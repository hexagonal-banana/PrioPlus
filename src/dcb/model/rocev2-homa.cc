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
 * 
 * Author:Haiwen Guan<blueroaring_hwguan@163.com>
 */

#include "rocev2-homa.h"

#include "rocev2-socket.h"

#include "ns3/ipv4-global-routing.h"
#include "ns3/log.h"
#include "ns3/node.h"
#include "ns3/simulator.h"

#include <iostream>

namespace ns3
{

//Identify nodes as receiver
std::map<uint32_t, Ptr<HomaNodeScheduler>> HomaNodeScheduler::m_nodeSchedulers;

NS_OBJECT_ENSURE_REGISTERED(HomaNodeScheduler);

TypeId
HomaNodeScheduler::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::HomaNodeScheduler").SetParent<Object>().AddConstructor<HomaNodeScheduler>();
    return tid;
}

HomaNodeScheduler::HomaNodeScheduler()
{
    //set as number of scheduled priority
    m_overcommitLevel = 2;
}

HomaNodeScheduler::~HomaNodeScheduler()
{
}

Ptr<HomaNodeScheduler>
HomaNodeScheduler::Get(uint32_t nodeId)
{
    if (m_nodeSchedulers.find(nodeId) == m_nodeSchedulers.end())
    {
        m_nodeSchedulers[nodeId] = CreateObject<HomaNodeScheduler>();
    }
    return m_nodeSchedulers[nodeId];
}

NS_LOG_COMPONENT_DEFINE("RoCEv2Homa");

NS_OBJECT_ENSURE_REGISTERED(RoCEv2Homa);

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
                                          UintegerValue(0x1F), // Highest Priority
                                          MakeUintegerAccessor(&RoCEv2Homa::m_unscheduledPrio),
                                          MakeUintegerChecker<uint32_t>())
                            .AddAttribute("ScheduledPrio",
                                          "Priority for scheduled packets",
                                          UintegerValue(0x00), // Lower Priority
                                          MakeUintegerAccessor(&RoCEv2Homa::m_scheduledPrio),
                                          MakeUintegerChecker<uint32_t>())
                            .AddAttribute("GrantPrio",
                                          "Priority for grants",
                                          UintegerValue(0x1F), // Highest Priority;
                                          MakeUintegerAccessor(&RoCEv2Homa::m_grantPrio),
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
    m_uniqueRecvedBytes = 0;
    m_unscheduledBytes = 10000;
    m_rttBytes = 10000;
    m_unscheduledPrio = 0x1F; // Scheduled use 0,1; Unscheduled use 2-7
    m_scheduledPrio = 0x00;
    m_stats = std::make_shared<Stats>();
}

void
RoCEv2Homa::SetReady()
{
    NS_LOG_FUNCTION(this);
    // Initial credit allows sending unscheduled bytes

    // Calculate RTTBytes: RTT × Bandwidth
    if (m_sockState->GetDeviceRate() != nullptr && m_sockState->GetBaseRtt().IsStrictlyPositive())
    {
        // RTTBytes = Bandwidth (bytes/sec) × RTT (seconds)
        uint64_t rttBytes = static_cast<uint64_t>(m_sockState->GetDeviceRate()->GetBitRate() / 8 *
                                                  m_sockState->GetBaseRtt().GetSeconds());

        // Set m_unscheduledBytes to RTTBytes to fully utilize the link in first RTT
        m_unscheduledBytes =
            static_cast<uint32_t>(std::min(rttBytes, static_cast<uint64_t>(UINT32_MAX)));

        /*std::cout << "Homa: Calculated RTTBytes = " << rttBytes
                  << " bytes (Bandwidth: " << m_sockState->GetDeviceRate()->GetBitRate() << " bps"
                  << ", RTT: " << m_sockState->GetBaseRtt().GetMicroSeconds() << " us)"
                  << std::endl;
        std::cout << "Homa: Setting UnscheduledBytes to " << m_unscheduledBytes << " bytes"
                  << std::endl;*/
    }
    else
    {
        std::cout << "Homa: Warning - Cannot calculate RTTBytes, using default value" << std::endl;
    }

    // Set initial credit to allow sending unscheduled bytes
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
        m_flowId = m_sockState->GetFlowId();
    }

    if (m_flowId == 0)
    {
        std::cout << "warning:m_flowId=0 " << std::endl;
    }
    uint8_t isUnscheduled = (m_bytesSended < m_unscheduledBytes) ? 1 : 0;
    HomaDataTag tag(m_flowId, m_msgSize, isUnscheduled);
    packet->AddPacketTag(tag);
    // Priority Logic: Use message size distribution (CDF approximation)
    // For now, simple logic: shorter messages get higher unscheduled priority
    uint32_t prio = m_unscheduledPrio;
    //infact we should use [280, 450, 700, 1100, 2500, 10000] as the threshold,but maybe our impletation have many big flows,so we change it
    if (m_msgSize < 1000)
        prio = 0x1F; // 7
    else if (m_msgSize < 10000)
        prio = 0x1A; // 6
    else if (m_msgSize < 100000)
        prio = 0x16; // 5
    else if (m_msgSize < 1000000)
        prio = 0x10; // 4
    else if (m_msgSize < 10000000)
        prio = 0x0F; // 3
    else
        prio = 0x0A; // 2

    // Priority Logic
    SocketIpTosTag ipTosTag;
    if (m_bytesSended < m_unscheduledBytes)
    {
       // std::cout << "unscheduled data"
         //         << " flowId=" << m_flowId << std::endl;
        ipTosTag.SetTos(prio);
    }
    else
    {
      //  std::cout << "scheduled data"
              //    << " flowId=" << m_flowId << std::endl;
        ipTosTag.SetTos(m_scheduledPrio);
    }
    packet->ReplacePacketTag(ipTosTag);

    m_bytesSended += packet->GetSize();
    //std::cout << "flow=" << m_flowId << "m_bytesSended=" << m_bytesSended
           //   << "m_unsbytes=" << m_unscheduledBytes << std::endl;
}

void
RoCEv2Homa::UpdateStateRecvData(Ptr<Packet> packet, const RoCEv2Header& roce)
{
    NS_LOG_FUNCTION(this << packet);

    HomaDataTag tag;
    uint8_t isUnscheduled = 0;
    if (packet->PeekPacketTag(tag))
    {
        m_flowId = tag.GetFlowId();
        m_msgSize = tag.GetMsgSize();
        isUnscheduled = tag.GetIsUnscheduled();
    }

    //uint32_t packetOffset = roce.GetPSN();
    uint32_t packetSize = packet->GetSize();

    m_recvedBytes += packetSize;
    //std::cout << "Homa: New packet received at offset " << packetOffset << ", size " << packetSize
      //        << ", unique bytes now: " << m_recvedBytes << ", flowId:" << m_flowId << std::endl;
    uint32_t nodeId = Simulator::GetContext();
    Ptr<HomaNodeScheduler> scheduler = HomaNodeScheduler::Get(nodeId);
    scheduler->UpdateFlow(m_flowId, m_msgSize, m_recvedBytes, this);
    scheduler->CheckSchedule(m_sockState->GetPacketSize(), packetSize, m_flowId, isUnscheduled);
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

        uint64_t currentCredit = m_sockState->GetCredit();
        //std::cout << "Rcv ACK" << grantedOffset << " " << currentCredit
          //        << "  flowid:" << m_sockState->GetFlowId() << std::endl;
        m_sockState->SetCredit(grantedOffset + currentCredit);
        m_scheduledPrio = tag.GetPriority();
        //std::cout << "now m_scheduledPrio=" << m_scheduledPrio << std::endl;
        // Trigger sending
        if (!m_sendPendingDataCb.IsNull())
        {
            m_sendPendingDataCb();
        }
        // }
    }
}

void
RoCEv2Homa::SendGrantACK(uint32_t grantOffset, uint32_t priority)
{
    NS_LOG_FUNCTION(this << grantOffset << (uint32_t)priority);
    //std::cout << "send priority" << (uint32_t)priority << "send flowid:" << m_sockState->GetFlowId()
      //        << std::endl;

    // Calculate optimal grant size based on RTT and bandwidth
    uint32_t optimalGrantSize = grantOffset; // Default to packet size

   

    HomaGrantTag tag(m_flowId, optimalGrantSize, priority);
    /* std::cout << "Sending Grant ACK with optimal size: " << optimalGrantSize
               << ", priority: " << tag.GetPriority() << std::endl;*/


    CongestionTypeTag ctTag(GetTypeId().GetUid());
    SocketIpTosTag ipTosTag;
    ipTosTag.SetTos(m_grantPrio);
    std::vector<std::reference_wrapper<const Tag>> packetTags{ctTag, tag, ipTosTag};

    // psn 0, isRequest=false
    m_sendOutbandPktCb(0, false, packetTags);
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
    bDetailedSenderStats = true;
}

void
RoCEv2Homa::Stats::CollectAndCheck()
{
}

// -------------------------------------------------------------------------
// HomaNodeScheduler Implementation
// -------------------------------------------------------------------------
void
HomaNodeScheduler::UpdateFlow(uint32_t flowId,
                              uint32_t msgSize,
                              uint32_t uniqueRecvedBytes, // Todo: add retransmission and out of order logic
                              Ptr<RoCEv2Homa> flow)
{
    FlowState& state = m_activeFlows[flowId];
    state.msgSize = msgSize;
    state.flow = flow;
    //std::cout << "flowId:" << flowId << "size" << m_activeFlows.size() << std::endl;
    state.recvedBytes = uniqueRecvedBytes; // Todo:Use unique bytes to avoid retransmission interference
    state.lastUpdate = Simulator::Now();
    //Send final grant(offset 0) to make flow ended
    if (uniqueRecvedBytes >= msgSize)
    {
        //std::cout << "HomaScheduler: Flow " << flowId << " completed with " << uniqueRecvedBytes
                  //<< "/" << msgSize << " unique bytes" << std::endl;
        Simulator::Schedule(NanoSeconds(100), &RoCEv2Homa::SendGrantACK, flow, 0, 0x00);
    }
}

void
HomaNodeScheduler::CheckSchedule(uint32_t packetSize,
                                 uint32_t realSize,
                                 uint32_t currentFlowId,
                                 uint8_t isUnscheduled)
{
    //bug needs to fix:how to make scheduled data run 2 priority but not lead to outoforder arriving
    //std::cout << "Active flows size=" << m_activeFlows.size() << std::endl;
    // Clean up completed flows from m_activeFlows
    for (auto it = m_activeFlows.begin(); it != m_activeFlows.end();)
    {
        if (it->second.recvedBytes >= it->second.msgSize)
        {
            //std::cout << "erased completed flow " << it->first << std::endl;
            it = m_activeFlows.erase(it);
        }
        else
        {
            ++it;
        }
    }

    // Count currently active flows
    uint32_t activeCount = 0;
    std::vector<uint32_t> inactiveFlows;
    for (auto& kv : m_activeFlows)
    {
        if (kv.second.isActive)
        {
            activeCount++;
        }
        else
        {
            inactiveFlows.push_back(kv.first);
        }
    }

    //std::cout << "inactiveflowsize:" << inactiveFlows.size() << std::endl;
    // Activate as many inactive flows as we have capacity for
    for (uint32_t id : inactiveFlows)
    {
      //  std::cout << "inactiveid:" << id << std::endl;
        if (activeCount >= m_overcommitLevel)
        {
            break;
        }
        m_activeFlows[id].isActive = true;
        activeCount++;
        // Send initial PROACTIVE grant to wake it up
        FlowState& state = m_activeFlows[id];
        uint32_t grantStep = packetSize; // initial grant size
        uint32_t newGrant = state.grantedBytes + grantStep;
        // Determine priority for this newly activated flow
        //Due to out of order bug,do not use fixed priority.
        // uint32_t prio = (id % 2 == 0) ? 0 : 6;
        //std::cout << "inactive send grant start" << std::endl;
        state.flow->SendGrantACK(grantStep, 0);
        state.grantedBytes = newGrant;
    }

    // Now process the current packet's response
    if (m_activeFlows.find(currentFlowId) != m_activeFlows.end())
    {
        FlowState& state = m_activeFlows[currentFlowId];
        state.msgBytes += realSize;
        //Due to out of order bug,do not use fixed priority.
        //uint32_t prio = (currentFlowId % 2 == 0) ? 0 : 6;
        //std::cout << "msgBytes=" << state.msgBytes << "size=" << state.msgSize
          //        << "flowId:" << currentFlowId << std::endl;
        if (state.isActive)
        {
            // Flow is ACTIVE.
            // Send normal grant for both unscheduled and scheduled, if we haven't already.
            // We might have just activated it above and sent a proactive grant,
            // but we can send another grant to keep pipeline full based on received packet.
           
            uint32_t grantStep = packetSize;
            uint32_t newGrant = state.grantedBytes + grantStep;
            //std::cout << "active send ACK"
              //      << "flowId:" << currentFlowId << std::endl;
            state.flow->SendGrantACK(grantStep, 0);
            state.grantedBytes = newGrant;
        }
        else
        {
            // Flow is INACTIVE.
            // If unscheduled, send an ACK (grant=0)
            if (isUnscheduled)
            {
                //std::cout << "inactive send ACK"
                  //        << "flowId" << currentFlowId << std::endl;
                state.flow->SendGrantACK(0, 0);
            }
            else
            {
                //Should not reach here
                std::cout << "Warning:should not reach this" << std::endl;
            }
        }
    }
    //no overcommit implementation
 /*   FlowState& state = m_activeFlows[currentFlowId];
    uint32_t grantStep = packetSize;
    uint32_t newGrant = state.grantedBytes + grantStep;
    state.flow->SendGrantACK(grantStep, 0);
    state.grantedBytes = newGrant;*/
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
    return sizeof(m_flowId) + sizeof(m_msgSize) + sizeof(m_isUnscheduled);
}

void
HomaDataTag::Serialize(TagBuffer i) const
{
    i.WriteU32(m_flowId);
    i.WriteU32(m_msgSize);
    i.WriteU8(m_isUnscheduled);
}

void
HomaDataTag::Deserialize(TagBuffer i)
{
    m_flowId = i.ReadU32();
    m_msgSize = i.ReadU32();
    m_isUnscheduled = i.ReadU8();
}

void
HomaDataTag::Print(std::ostream& os) const
{
    os << "FlowId=" << m_flowId << " MsgSize=" << m_msgSize
       << " Unscheduled=" << (int)m_isUnscheduled;
}

HomaDataTag::HomaDataTag()
    : m_flowId(0),
      m_msgSize(0),
      m_isUnscheduled(0)
{
}

HomaDataTag::HomaDataTag(uint32_t flowId, uint32_t msgSize, uint8_t isUnscheduled)
    : m_flowId(flowId),
      m_msgSize(msgSize),
      m_isUnscheduled(isUnscheduled)
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

void
HomaDataTag::SetIsUnscheduled(uint8_t isUnscheduled)
{
    m_isUnscheduled = isUnscheduled;
}

uint8_t
HomaDataTag::GetIsUnscheduled() const
{
    return m_isUnscheduled;
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

HomaGrantTag::HomaGrantTag(uint32_t flowId, uint32_t grantOffset, uint32_t priority)
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
HomaGrantTag::SetPriority(uint32_t p)
{
    m_priority = p;
}

uint32_t
HomaGrantTag::GetPriority() const
{
    return m_priority;
}

} // namespace ns3
