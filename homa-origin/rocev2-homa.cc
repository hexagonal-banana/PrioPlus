/*
 * Copyright (c) 2024
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "rocev2-homa.h"

#include "rocev2-socket.h"

#include <algorithm>
#include <functional>
#include <iostream>
#include <vector>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("RoCEv2Homa");

NS_OBJECT_ENSURE_REGISTERED(RoCEv2Homa);

std::map<uint32_t, Ptr<HomaScheduler>> RoCEv2Homa::m_nodeSchedulers;

TypeId
RoCEv2Homa::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::RoCEv2Homa")
            .SetParent<RoCEv2CreditCc>()
            .AddConstructor<RoCEv2Homa>()
            .SetGroupName("DCB")
            .AddAttribute("UnscheduledBytes",
                          "Initial bytes sent without grant",
                          UintegerValue(10000),
                          MakeUintegerAccessor(&RoCEv2Homa::m_unscheduledBytes),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("RttBytes",
                          "RTTbytes: target amount of granted-but-not-received data",
                          UintegerValue(10000),
                          MakeUintegerAccessor(&RoCEv2Homa::m_rttBytes),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("UnscheduledPrio",
                          "Base priority for unscheduled packets",
                          UintegerValue(2),
                          MakeUintegerAccessor(&RoCEv2Homa::m_unscheduledPrio),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("ScheduledPrio",
                          "Base priority for scheduled packets",
                          UintegerValue(0),
                          MakeUintegerAccessor(&RoCEv2Homa::m_scheduledPrio),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("OvercommitLevel",
                          "Maximum number of active flows at a receiver",
                          UintegerValue(2),
                          MakeUintegerAccessor(&RoCEv2Homa::m_overcommitLevel),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("OutstandingRpcThreshold",
                          "Threshold for incast detection",
                          UintegerValue(100),
                          MakeUintegerAccessor(&RoCEv2Homa::m_outstandingRpcThreshold),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("ResendTimeout",
                          "Timeout for RESEND packets",
                          TimeValue(MilliSeconds(5)),
                          MakeTimeAccessor(&RoCEv2Homa::m_resendTimeout),
                          MakeTimeChecker());
    return tid;
}

std::string
RoCEv2Homa::GetName() const
{
    return "RoCEv2Homa";
}

RoCEv2Homa::RoCEv2Homa()
    : RoCEv2CreditCc()
{
    NS_LOG_FUNCTION(this);
    Init();
}

RoCEv2Homa::RoCEv2Homa(Ptr<RoCEv2SocketState> sockState)
    : RoCEv2CreditCc(sockState)
{
    NS_LOG_FUNCTION(this);
    Init();
}

RoCEv2Homa::~RoCEv2Homa()
{
    NS_LOG_FUNCTION(this);
}

void
RoCEv2Homa::Init()
{
    NS_LOG_FUNCTION(this);
    RegisterCongestionType(GetTypeId());
    m_flowId = 0;
    m_msgSize = 0;
    m_bytesSended = 0;
    m_recvedBytes = 0;
    m_unscheduledBytes = 10000;
    m_rttBytes = 10000;
    m_unscheduledPrio = 2; // Scheduled use 0, 1; Unscheduled use 2-7
    m_scheduledPrio = 0;
    m_isIncast = false;
    m_outstandingRpcThreshold = 100;
    m_resendTimeout = MilliSeconds(5);
    m_overcommitLevel = 2;
    m_stats = std::make_shared<Stats>();
}

void
RoCEv2Homa::SetReady()
{
    NS_LOG_FUNCTION(this);
    // Incast detection
    Ptr<HomaScheduler> scheduler = GetNodeScheduler();
    if (scheduler && scheduler->GetActiveFlowCount() > m_outstandingRpcThreshold)
    {
        m_isIncast = true;
        m_unscheduledBytes = 500; // Reduce unscheduled limit
    }

    // Initial credit allows sending unscheduled bytes
    uint32_t pktSize = m_sockState->GetPacketSize();
    if (pktSize > 0)
    {
        m_sockState->SetCredit(m_unscheduledBytes / pktSize);
    }
}

void
RoCEv2Homa::SetFlowId(uint32_t flowId)
{
    NS_LOG_FUNCTION(this << flowId);
    m_flowId = flowId;
}

void
RoCEv2Homa::UpdateStateSend(Ptr<Packet> packet)
{
    NS_LOG_FUNCTION(this << packet);

    // Add Homa Data Tag
    // We assume the msg size is known from socket state or we infer it
    if (m_msgSize == 0)
    {
        m_msgSize = m_sockState->GetFlowTotalSize();
    }

    if (m_flowId == 0)
    {
        // Just use pointer address as simple ID if we can't get strict flow ID
        // Or generate one
        // m_flowId = (uint32_t)(uintptr_t)this;
        // Better: use socket ports
        // But we don't have easy access to IP/Ports here without storing them.
        // RoCEv2SocketState does not store src/dst IP/Port directly in a convenient way for unique
        // ID generation? Actually RoCEv2SocketState has m_daddr, but not src addr/port easily.
        // Let's generate a random ID if 0 or trust the tag if already present?
    }

    HomaDataTag tag(m_flowId, m_msgSize);
    packet->AddPacketTag(tag);

    // Priority Logic: Use message size distribution (CDF approximation)
    // For now, simple logic: shorter messages get higher unscheduled priority
    uint8_t prio = m_unscheduledPrio;
    if (m_msgSize < 1000)
        prio = 7;
    else if (m_msgSize < 10000)
        prio = 6;
    else if (m_msgSize < 100000)
        prio = 5;
    else if (m_msgSize < 1000000)
        prio = 4;
    else
        prio = 3;

    SocketIpTosTag ipTosTag;
    if (m_bytesSended < m_unscheduledBytes)
    {
        ipTosTag.SetTos(prio << 2);
    }
    else
    {
        ipTosTag.SetTos(m_scheduledPrio << 2); // Initial scheduled prio, will be updated by grants
    }
    packet->ReplacePacketTag(ipTosTag);

    m_bytesSended += packet->GetSize();

    // Parent class might add CreditRequestTag, we should prevent that or ignore it.
    // RoCEv2CreditCc adds CreditRequestTag at end of flow. We can keep it or not.
    // Ideally we don't call parent UpdateStateSend to avoid pollution
    // RoCEv2CreditCc::UpdateStateSend(packet);
}

void
RoCEv2Homa::UpdateStateRecvData(Ptr<Packet> packet, const RoCEv2Header& roce)
{
    NS_LOG_FUNCTION(this << packet);

    HomaDataTag tag;
    if (packet->PeekPacketTag(tag))
    {
        m_flowId = tag.GetFlowId();
        m_msgSize = tag.GetMsgSize();
    }

    m_recvedBytes += packet->GetSize();

    // Start/Reset Resend Timer
    if (m_resendEvent.IsRunning())
    {
        m_resendEvent.Cancel();
    }
    m_resendEvent = Simulator::Schedule(m_resendTimeout, &RoCEv2Homa::ProcessResend, this);

    Ptr<HomaScheduler> scheduler = GetNodeScheduler();
    if (scheduler)
    {
        scheduler->UpdateFlow(m_flowId, m_msgSize, m_recvedBytes, this);
        scheduler->CheckSchedule(m_sockState->GetPacketSize(), m_rttBytes, m_overcommitLevel);
    }
}

void
RoCEv2Homa::UpdateStateWithRcvACK(Ptr<Packet> packet,
                                  const RoCEv2Header& roce,
                                  const uint32_t senderNextPSN)
{
    NS_LOG_FUNCTION(this << packet);

    HomaGrantTag tag;
    if (packet->PeekPacketTag(tag))
    {
        // This is a GRANT
        uint32_t grantedOffset = tag.GetGrantOffset();
        m_scheduledPrio = tag.GetPriority();

        uint32_t pktSize = m_sockState->GetPacketSize();
        // m_frontPsn is not directly accessible here, but RoCEv2SocketState has it?
        // Let's check RoCEv2SocketState. Actually RoCEv2SocketState doesn't have it.
        // DcbTxBuffer has it. Socket has it.
        // If we can't get frontPsn, we can just set credit to (grantedOffset - bytesSended) /
        // pktSize but that might be negative or wrap. Better: SetCredit(grantedOffset / pktSize) if
        // SetCredit is absolute. It seems SetCredit in ROCEv2-Homa implementation is intended to be
        // absolute packets.
        m_sockState->SetCredit(grantedOffset / pktSize);

        if (!m_sendPendingDataCb.IsNull())
        {
            m_sendPendingDataCb();
        }
    }
}

void
RoCEv2Homa::UpdateStateWithOutbandPkt(Ptr<Packet> packet,
                                      const RoCEv2Header& roce,
                                      const uint32_t senderNextPSN)
{
    NS_LOG_FUNCTION(this << packet);

    HomaResendTag resendTag;
    if (packet->PeekPacketTag(resendTag))
    {
        uint32_t offset = resendTag.GetOffset();
        uint32_t pktSize = m_sockState->GetPacketSize();
        uint32_t fromPsn = offset / pktSize;
        uint32_t toPsn = (offset + resendTag.GetLength()) / pktSize;

        DcbTxBuffer* txBuffer = m_sockState->GetTxBuffer();
        if (txBuffer)
        {
            txBuffer->RetransmitRange(fromPsn, std::min(toPsn, txBuffer->TotalSize()));
        }
    }
}

void
RoCEv2Homa::SendGrantACK(uint32_t grantOffset, uint8_t priority)
{
    NS_LOG_FUNCTION(this << grantOffset << (uint16_t)priority);
    HomaGrantTag tag(m_flowId, grantOffset, priority);
    CongestionTypeTag ctTag(GetTypeId().GetUid());
    std::vector<std::reference_wrapper<const Tag>> packetTags{ctTag, tag};
    m_sendOutbandPktCb(0, false, packetTags);
}

void
RoCEv2Homa::SendResend(uint32_t offset, uint32_t length)
{
    NS_LOG_FUNCTION(this << offset << length);
    HomaResendTag tag(m_flowId, offset, length);
    CongestionTypeTag ctTag(GetTypeId().GetUid());
    std::vector<std::reference_wrapper<const Tag>> packetTags{ctTag, tag};
    m_sendOutbandPktCb(0, false, packetTags);
}

void
RoCEv2Homa::ProcessResend()
{
    NS_LOG_FUNCTION(this);
    if (m_recvedBytes < m_msgSize)
    {
        SendResend(m_recvedBytes, m_rttBytes);
        m_resendEvent = Simulator::Schedule(m_resendTimeout, &RoCEv2Homa::ProcessResend, this);
    }
}

Ptr<HomaScheduler>
RoCEv2Homa::GetNodeScheduler()
{
    uint32_t nodeId = Simulator::GetContext();
    if (m_nodeSchedulers.find(nodeId) == m_nodeSchedulers.end())
    {
        m_nodeSchedulers[nodeId] = CreateObject<HomaScheduler>();
    }
    return m_nodeSchedulers[nodeId];
}

uint32_t
RoCEv2Homa::GetFlowId() const
{
    return m_flowId;
}

// -------------------------------------------------------------------------
// RoCEv2Homa::Stats Implementation
// -------------------------------------------------------------------------

RoCEv2Homa::Stats::Stats()
{
    bDetailedSenderStats = false;
}

void
RoCEv2Homa::Stats::CollectAndCheck()
{
}

// -------------------------------------------------------------------------
// HomaScheduler Implementation
// -------------------------------------------------------------------------

NS_OBJECT_ENSURE_REGISTERED(HomaScheduler);

TypeId
HomaScheduler::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::HomaScheduler").SetParent<Object>().AddConstructor<HomaScheduler>();
    return tid;
}

HomaScheduler::HomaScheduler()
{
    m_numScheduledPriorities = 2;   // P0, P1
    m_numUnscheduledPriorities = 6; // P2-P7
    m_overcommitLevel = m_numScheduledPriorities;
    m_rttBytes = 10000;
}

HomaScheduler::~HomaScheduler()
{
}

void
HomaScheduler::UpdateFlow(uint32_t flowId,
                          uint32_t msgSize,
                          uint32_t recvedBytes,
                          Ptr<RoCEv2Homa> flow)
{
    FlowState& state = m_activeFlows[flowId];
    state.msgSize = msgSize;
    state.recvedBytes = recvedBytes;
    state.flow = flow;
    state.lastUpdate = Simulator::Now();
    if (state.grantedBytes < recvedBytes)
        state.grantedBytes = recvedBytes;

    if (recvedBytes >= msgSize)
    {
        Simulator::Schedule(NanoSeconds(100), &RoCEv2Homa::SendGrantACK, flow, 0, 0);
        RemoveFlow(flowId);
    }
}

void
HomaScheduler::RemoveFlow(uint32_t flowId)
{
    m_activeFlows.erase(flowId);
}

uint32_t
HomaScheduler::GetActiveFlowCount() const
{
    return m_activeFlows.size();
}

void
HomaScheduler::CheckSchedule(uint32_t packetSize)
{
    if (m_activeFlows.empty())
        return;

    // SRPT: Sort flows by remaining bytes
    std::vector<uint32_t> sortedFlows;
    for (const auto& [id, state] : m_activeFlows)
    {
        sortedFlows.push_back(id);
    }

    std::sort(sortedFlows.begin(), sortedFlows.end(), [this](uint32_t a, uint32_t b) {
        uint32_t remA = m_activeFlows.at(a).msgSize - m_activeFlows.at(a).recvedBytes;
        uint32_t remB = m_activeFlows.at(b).msgSize - m_activeFlows.at(b).recvedBytes;
        return remA < remB;
    });

    // Grant top N (Overcommitment)
    uint32_t k = std::min((uint32_t)sortedFlows.size(), m_overcommitLevel);
    for (uint32_t i = 0; i < k; ++i)
    {
        uint32_t id = sortedFlows[i];
        FlowState& state = m_activeFlows[id];

        // Priority assignment to avoid preemption lag
        // i=0 (shortest) gets highest among active scheduled priorities
        uint8_t prio = (uint8_t)(k - 1 - i);

        uint32_t newGrant = state.recvedBytes + m_rttBytes;
        if (newGrant > state.msgSize)
            newGrant = state.msgSize;

        if (newGrant > state.grantedBytes)
        {
            state.flow->SendGrantACK(newGrant, prio);
            state.grantedBytes = newGrant;
        }
    }
}

// -------------------------------------------------------------------------
// Tags Implementation
// -------------------------------------------------------------------------

TypeId
HomaDataTag::GetTypeId()
{
    static TypeId tid = TypeId("ns3::HomaDataTag").SetParent<Tag>().AddConstructor<HomaDataTag>();
    return tid;
}

TypeId
HomaDataTag::GetInstanceTypeId() const
{
    return GetTypeId();
}

uint32_t
HomaDataTag::GetSerializedSize() const
{
    return sizeof(m_flowId) + sizeof(m_msgSize);
}

void
HomaDataTag::Serialize(TagBuffer i) const
{
    i.WriteU32(m_flowId);
    i.WriteU32(m_msgSize);
}

void
HomaDataTag::Deserialize(TagBuffer i)
{
    m_flowId = i.ReadU32();
    m_msgSize = i.ReadU32();
}

void
HomaDataTag::Print(std::ostream& os) const
{
    os << "FlowId=" << m_flowId << " MsgSize=" << m_msgSize;
}

HomaDataTag::HomaDataTag()
    : m_flowId(0),
      m_msgSize(0)
{
}

HomaDataTag::HomaDataTag(uint32_t flowId, uint32_t msgSize)
    : m_flowId(flowId),
      m_msgSize(msgSize)
{
}

void
HomaDataTag::SetFlowId(uint32_t id)
{
    m_flowId = id;
}

uint32_t
HomaDataTag::GetFlowId() const
{
    return m_flowId;
}

void
HomaDataTag::SetMsgSize(uint32_t size)
{
    m_msgSize = size;
}

uint32_t
HomaDataTag::GetMsgSize() const
{
    return m_msgSize;
}

TypeId
HomaGrantTag::GetTypeId()
{
    static TypeId tid = TypeId("ns3::HomaGrantTag").SetParent<Tag>().AddConstructor<HomaGrantTag>();
    return tid;
}

TypeId
HomaGrantTag::GetInstanceTypeId() const
{
    return GetTypeId();
}

uint32_t
HomaGrantTag::GetSerializedSize() const
{
    return sizeof(m_flowId) + sizeof(m_grantOffset) + sizeof(m_priority);
}

void
HomaGrantTag::Serialize(TagBuffer i) const
{
    i.WriteU32(m_flowId);
    i.WriteU32(m_grantOffset);
    i.WriteU8(m_priority);
}

void
HomaGrantTag::Deserialize(TagBuffer i)
{
    m_flowId = i.ReadU32();
    m_grantOffset = i.ReadU32();
    m_priority = i.ReadU8();
}

void
HomaGrantTag::Print(std::ostream& os) const
{
    os << "FlowId=" << m_flowId << " GrantOffset=" << m_grantOffset << " Prio=" << (int)m_priority;
}

HomaGrantTag::HomaGrantTag()
    : m_flowId(0),
      m_grantOffset(0),
      m_priority(0)
{
}

HomaGrantTag::HomaGrantTag(uint32_t flowId, uint32_t grantOffset, uint8_t priority)
    : m_flowId(flowId),
      m_grantOffset(grantOffset),
      m_priority(priority)
{
}

void
HomaGrantTag::SetFlowId(uint32_t id)
{
    m_flowId = id;
}

uint32_t
HomaGrantTag::GetFlowId() const
{
    return m_flowId;
}

void
HomaGrantTag::SetGrantOffset(uint32_t os)
{
    m_grantOffset = os;
}

uint32_t
HomaGrantTag::GetGrantOffset() const
{
    return m_grantOffset;
}

void
HomaGrantTag::SetPriority(uint8_t p)
{
    m_priority = p;
}

uint8_t
HomaGrantTag::GetPriority() const
{
    return m_priority;
}

// -------------------------------------------------------------------------
// HomaResendTag Implementation
// -------------------------------------------------------------------------

TypeId
HomaResendTag::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::HomaResendTag").SetParent<Tag>().AddConstructor<HomaResendTag>();
    return tid;
}

TypeId
HomaResendTag::GetInstanceTypeId() const
{
    return GetTypeId();
}

uint32_t
HomaResendTag::GetSerializedSize() const
{
    return sizeof(m_flowId) + sizeof(m_offset) + sizeof(m_length);
}

void
HomaResendTag::Serialize(TagBuffer i) const
{
    i.WriteU32(m_flowId);
    i.WriteU32(m_offset);
    i.WriteU32(m_length);
}

void
HomaResendTag::Deserialize(TagBuffer i)
{
    m_flowId = i.ReadU32();
    m_offset = i.ReadU32();
    m_length = i.ReadU32();
}

void
HomaResendTag::Print(std::ostream& os) const
{
    os << "FlowId=" << m_flowId << " Offset=" << m_offset << " Len=" << m_length;
}

HomaResendTag::HomaResendTag()
    : m_flowId(0),
      m_offset(0),
      m_length(0)
{
}

HomaResendTag::HomaResendTag(uint32_t flowId, uint32_t offset, uint32_t length)
    : m_flowId(flowId),
      m_offset(offset),
      m_length(length)
{
}

void
HomaResendTag::SetFlowId(uint32_t id)
{
    m_flowId = id;
}

uint32_t
HomaResendTag::GetFlowId() const
{
    return m_flowId;
}

void
HomaResendTag::SetOffset(uint32_t os)
{
    m_offset = os;
}

uint32_t
HomaResendTag::GetOffset() const
{
    return m_offset;
}

void
HomaResendTag::SetLength(uint32_t len)
{
    m_length = len;
}

uint32_t
HomaResendTag::GetLength() const
{
    return m_length;
}

} // namespace ns3
