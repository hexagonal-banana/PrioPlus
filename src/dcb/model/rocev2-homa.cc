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

#include "ns3/log.h"
#include "ns3/node.h"
#include "ns3/simulator.h"

#include <iostream>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("RoCEv2Homa");

NS_OBJECT_ENSURE_REGISTERED(RoCEv2Homa);

std::map<uint32_t, Ptr<HomaScheduler>> RoCEv2Homa::m_nodeSchedulers;

TypeId
RoCEv2Homa::GetTypeId()
{
    static TypeId tid = TypeId("ns3::RoCEv2Homa")
                            .SetParent<RoCEv2CreditCc>()
                            .AddConstructor<RoCEv2Homa>()
                            .SetGroupName("DCB")
                            .AddAttribute("UnscheduledBytes",
                                          "Bytes sent without grant in the first RTT",
                                          UintegerValue(10000), // Default 10KB
                                          MakeUintegerAccessor(&RoCEv2Homa::m_unscheduledBytes),
                                          MakeUintegerChecker<uint32_t>())
                            .AddAttribute("UnscheduledPrio",
                                          "Priority for unscheduled packets",
                                          UintegerValue(7), // Highest Priority
                                          MakeUintegerAccessor(&RoCEv2Homa::m_unscheduledPrio),
                                          MakeUintegerChecker<uint32_t>())
                            .AddAttribute("ScheduledPrio",
                                          "Priority for scheduled packets",
                                          UintegerValue(0), // Lower Priority
                                          MakeUintegerAccessor(&RoCEv2Homa::m_scheduledPrio),
                                          MakeUintegerChecker<uint32_t>());
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
    m_unscheduledPrio = 7;
    m_scheduledPrio = 0;
    m_stats = std::make_shared<Stats>();
}

void
RoCEv2Homa::SetReady()
{
    NS_LOG_FUNCTION(this);
    // Initial credit allows sending unscheduled bytes
    m_sockState->SetCredit(m_unscheduledBytes);
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

    // Priority Logic
    SocketIpTosTag ipTosTag;
    if (m_bytesSended < m_unscheduledBytes)
    {
        ipTosTag.SetTos(m_unscheduledPrio << 2); // Shift for DSCP/ToS field
    }
    else
    {
        ipTosTag.SetTos(m_scheduledPrio << 2);
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

    Ptr<HomaScheduler> scheduler = GetNodeScheduler();
    if (scheduler)
    {
        scheduler->UpdateFlow(m_flowId, m_msgSize, m_recvedBytes, this);
        scheduler->CheckSchedule(m_sockState->GetPacketSize());
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
        // Update credit
        // Current credit = grantedOffset - bytesSent?
        // Or simply set credit to grantedOffset - bytesAcked?
        // RoCEv2Socket uses credit as "bytes allowed to be sent relative to ...?"
        // check RoCEv2SocketState::SetCredit.
        // It seems credit is absolute "credit" value? No, usually "credit" in DcbTxBuffer means
        // "allow to send X bytes". DcbTxBuffer::CouldSend checks: (m_maxSentPsn + 1) * m_packetSize
        // <= m_credit? Let's check DcbTxBuffer. It seems RoCEv2SocketState::SetCredit sets an
        // absolute value of bytes that can be sent total? RoCEv2CreditCc:
        // m_sockState->SetCredit(m_sockState->GetCredit() + m_sockState->GetPacketSize()); So
        // credit increases. So `m_credit` in socket state represents the "Authorized Window Right
        // Edge" in bytes (cumulative).

        uint64_t currentCredit = m_sockState->GetCredit();
        // if (grantedOffset > currentCredit)
        // {
        std::cout << "Rcv ACK" << grantedOffset << " " << currentCredit << std::endl;
        m_sockState->SetCredit(grantedOffset + currentCredit);
        // Trigger sending
        if (!m_sendPendingDataCb.IsNull())
        {
            m_sendPendingDataCb();
        }
        // }
    }
}

void
RoCEv2Homa::SendGrantACK(uint32_t grantOffset, uint8_t priority)
{
    NS_LOG_FUNCTION(this << grantOffset << (uint16_t)priority);
    std::cout << "Sending Grant ACK" << std::endl;
    HomaGrantTag tag(m_flowId, grantOffset, priority);
    // Also we need to specify CongestionTypeTag to route it to correct CC on receiver?
    // Actually out-of-band packets are demuxed by RoCEv2Socket to the correct flow.
    // If it's a "credit" packet, RoCEv2Socket might handle it?
    // RoCEv2CreditCc uses m_sendOutbandPktCb.

    CongestionTypeTag ctTag(GetTypeId().GetUid());
    std::vector<std::reference_wrapper<const Tag>> packetTags{ctTag, tag};

    // psn 0, isRequest=false
    m_sendOutbandPktCb(0, false, packetTags);
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
    m_overcommitLevel = 1; // Simplest implementation
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

void
HomaScheduler::CheckSchedule(uint32_t packetSize)
{
    // SRPT: Sort flows by remaining bytes
    std::vector<uint32_t> sortedFlows;
    for (const auto& [id, state] : m_activeFlows)
    {
        sortedFlows.push_back(id);
        if (state.recvedBytes >= state.msgSize)
        {
            RemoveFlow(id);
        }
    }

    std::sort(sortedFlows.begin(), sortedFlows.end(), [this](uint32_t a, uint32_t b) {
        uint32_t remA = m_activeFlows[a].msgSize - m_activeFlows[a].recvedBytes;
        uint32_t remB = m_activeFlows[b].msgSize - m_activeFlows[b].recvedBytes;
        return remA < remB;
    });

    // Grant top N
    uint32_t count = 0;
    for (uint32_t id : sortedFlows)
    {
        if (count >= m_overcommitLevel)
            break;

        FlowState& state = m_activeFlows[id];
        uint32_t grantStep = packetSize; // Grant 10KB at a time
        uint32_t newGrant = state.grantedBytes + grantStep;
        if (newGrant > state.msgSize)
            newGrant = state.msgSize;

        if (newGrant >= state.grantedBytes)
        {
            state.flow->SendGrantACK(grantStep, 0); // Priority 0 (highest?)
            state.grantedBytes = newGrant;
        }
        count++;
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

} // namespace ns3
