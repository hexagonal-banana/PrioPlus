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

#include "ns3/ipv4-global-routing.h"
#include "ns3/log.h"
#include "ns3/node.h"
#include "ns3/simulator.h"

#include <iostream>

namespace ns3
{

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
    m_unscheduledPrio = 0x1F; // Scheduled use 0, 1; Unscheduled use 2-7
    m_scheduledPrio = 0x00;
    m_nodeScheduler = std::make_shared<HomaScheduler>();

    // 初始化乱序和丢包处理相关变量
    m_expectedPsn = 0;
    m_lostPacketCount = 0;
    m_outOfOrderBuffer.clear();

    // 初始化包偏移量跟踪数组
    m_receivedPackets.clear();
    m_packetSizes.clear();

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

        std::cout << "Homa: Calculated RTTBytes = " << rttBytes
                  << " bytes (Bandwidth: " << m_sockState->GetDeviceRate()->GetBitRate() << " bps"
                  << ", RTT: " << m_sockState->GetBaseRtt().GetMicroSeconds() << " us)"
                  << std::endl;
        std::cout << "Homa: Setting UnscheduledBytes to " << m_unscheduledBytes << " bytes"
                  << std::endl;
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
    std::cout << "flow=" << m_flowId << std::endl;
    HomaDataTag tag(m_flowId, m_msgSize);
    packet->AddPacketTag(tag);
    // Priority Logic: Use message size distribution (CDF approximation)
    // For now, simple logic: shorter messages get higher unscheduled priority
    uint32_t prio = m_unscheduledPrio;
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
        std::cout << "unscheduled data" << std::endl;
        ipTosTag.SetTos(prio);
    }
    else
    {
        std::cout << "scheduled data" << std::endl;
        ipTosTag.SetTos(m_scheduledPrio);
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

    // 获取包偏移量（使用PSN作为偏移量）
    uint32_t packetOffset = roce.GetPSN();
    uint32_t packetSize = packet->GetSize();

    // 更新总接收字节数（包括重传）
    m_recvedBytes += packetSize;

    // 使用包偏移量进行精确跟踪，避免重传重复计算
    SetPacketReceived(packetOffset, packetSize);

    // 添加序列号跟踪用于乱序检测
    uint32_t currentPsn = roce.GetPSN();

    // 乱序检测和丢包统计
    if (m_expectedPsn == 0)
    {
        m_expectedPsn = currentPsn; // 初始化期望PSN
    }

    if (currentPsn > m_expectedPsn)
    {
        // 检测到丢包 - 统计丢失的包数量
        uint32_t lostPkts = currentPsn - m_expectedPsn;
        m_lostPacketCount += lostPkts;
        std::cout << "Homa: Detected " << lostPkts << " lost packets in flow " << m_flowId
                  << " (expected " << m_expectedPsn << ", got " << currentPsn << ")" << std::endl;
    }
    else if (currentPsn < m_expectedPsn)
    {
        // 乱序包 - 记录但不计入丢包
        std::cout << "Homa: Out-of-order packet detected in flow " << m_flowId << " (expected "
                  << m_expectedPsn << ", got " << currentPsn << ")" << std::endl;
        // 将乱序包加入缓冲区等待重排
        m_outOfOrderBuffer[currentPsn] = packet->Copy();
    }

    // 更新期望的下一个PSN（只在包是按序或超前到达时更新）
    if (currentPsn >= m_expectedPsn)
    {
        m_expectedPsn = currentPsn + 1;
    }

    // 处理缓冲区中的乱序包
    ProcessOutOfOrderBuffer();

    std::shared_ptr<HomaScheduler> scheduler = m_nodeScheduler;
    if (scheduler)
    {
        // 使用unique bytes进行调度决策，避免重传干扰
        scheduler->UpdateFlow(m_flowId, m_msgSize, m_uniqueRecvedBytes, this);
        scheduler->AddReadyToSendGrant(m_flowId);
        scheduler->CheckSchedule(m_sockState->GetPacketSize());
    }
}

// 新增：处理乱序缓冲区的方法
void
RoCEv2Homa::ProcessOutOfOrderBuffer()
{
    // 按顺序处理缓冲区中的包
    while (m_outOfOrderBuffer.find(m_expectedPsn) != m_outOfOrderBuffer.end())
    {
        Ptr<Packet> bufferedPacket = m_outOfOrderBuffer[m_expectedPsn];
        m_outOfOrderBuffer.erase(m_expectedPsn);

        // 注意：字节数已经在主流程中更新过了，这里只需要更新期望PSN
        m_expectedPsn++;

        std::cout << "Homa: Processed buffered packet, new expected PSN: " << m_expectedPsn
                  << std::endl;
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

        uint64_t currentCredit = m_sockState->GetCredit();
        // if (grantedOffset > currentCredit)
        // {
        std::cout << "Rcv ACK" << grantedOffset << " " << currentCredit << std::endl;
        m_sockState->SetCredit(grantedOffset + currentCredit);
        m_scheduledPrio = tag.GetPriority();
        std::cout << "now m_scheduledPrio=" << m_scheduledPrio << std::endl;
        // Trigger sending
        if (!m_sendPendingDataCb.IsNull())
        {
            m_sendPendingDataCb();
        }
        // }
    }
    else
    {
        // Trigger sending
        if (!m_sendPendingDataCb.IsNull())
        {
            m_sendPendingDataCb();
        }
    }
}

void
RoCEv2Homa::SendGrantACK(uint32_t grantOffset, uint32_t priority)
{
    NS_LOG_FUNCTION(this << grantOffset << (uint32_t)priority);
    std::cout << "send priority" << (uint32_t)priority << std::endl;

    // Calculate optimal grant size based on RTT and bandwidth
    uint32_t optimalGrantSize = m_sockState->GetPacketSize(); // Default to packet size

    /* if (m_sockState->GetDeviceRate() != nullptr &&
     m_sockState->GetBaseRtt().IsStrictlyPositive())
     {
         // Calculate how many packets can be sent per RTT to maintain full link utilization
         uint64_t rttBytes = static_cast<uint64_t>(
             m_sockState->GetDeviceRate()->GetBitRate() / 8 *
     m_sockState->GetBaseRtt().GetSeconds());

         // Grant enough credit to send approximately one RTT worth of data
         // But cap it to prevent excessive buffering
         uint32_t maxGrant = static_cast<uint32_t>(std::min(rttBytes / 2,
     static_cast<uint64_t>(1000000))); // Cap at 1MB optimalGrantSize = std::min(maxGrant,
     grantOffset);

         std::cout << "Homa Grant: RTTBytes=" << rttBytes
                   << ", GrantOffset=" << grantOffset
                   << ", OptimalGrant=" << optimalGrantSize << std::endl;
     }*/

    HomaGrantTag tag(m_flowId, optimalGrantSize, priority);
    /* std::cout << "Sending Grant ACK with optimal size: " << optimalGrantSize
               << ", priority: " << tag.GetPriority() << std::endl;*/

    // Also we need to specify CongestionTypeTag to route it to correct CC on receiver?
    // Actually out-of-band packets are demuxed by RoCEv2Socket to the correct flow.
    // If it's a "credit" packet, RoCEv2Socket might handle it?
    // RoCEv2CreditCc uses m_sendOutbandPktCb.

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

void
RoCEv2Homa::SetPacketReceived(uint32_t packetOffset, uint32_t packetSize)
{
    NS_LOG_FUNCTION(this << packetOffset << packetSize);

    // 扩展数组大小如果需要
    if (packetOffset >= m_receivedPackets.size())
    {
        m_receivedPackets.resize(packetOffset + 1, false);
        m_packetSizes.resize(packetOffset + 1, 0);
    }

    // 只有新接收的包才更新unique bytes
    if (!m_receivedPackets[packetOffset])
    {
        m_receivedPackets[packetOffset] = true;
        m_packetSizes[packetOffset] = packetSize;
        m_uniqueRecvedBytes += packetSize;
        std::cout << "Homa: New packet received at offset " << packetOffset << ", size "
                  << packetSize << ", unique bytes now: " << m_uniqueRecvedBytes << std::endl;
    }
    else
    {
        std::cout << "Homa: Duplicate packet detected at offset " << packetOffset << ", size "
                  << packetSize << std::endl;
    }
}

bool
RoCEv2Homa::IsPacketReceived(uint32_t packetOffset) const
{
    if (packetOffset >= m_receivedPackets.size())
        return false;
    return m_receivedPackets[packetOffset];
}

uint32_t
RoCEv2Homa::GetUniqueReceivedBytes() const
{
    return m_uniqueRecvedBytes;
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
// HomaScheduler Implementation
// -------------------------------------------------------------------------

TypeId
RoCEv2Homa::HomaScheduler::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::HomaScheduler").SetParent<Object>().AddConstructor<HomaScheduler>();
    return tid;
}

RoCEv2Homa::HomaScheduler::HomaScheduler()
{
    m_overcommitLevel = 1;
}

RoCEv2Homa::HomaScheduler::~HomaScheduler()
{
}

void
RoCEv2Homa::HomaScheduler::UpdateFlow(uint32_t flowId,
                                      uint32_t msgSize,
                                      uint32_t uniqueRecvedBytes, // Changed from recvedBytes
                                      Ptr<RoCEv2Homa> flow)
{
    FlowState& state = m_activeFlows[flowId];
    state.msgSize = msgSize;
    state.recvedBytes = uniqueRecvedBytes; // Use unique bytes to avoid retransmission interference
    state.flow = flow;
    state.lastUpdate = Simulator::Now();
    if (state.grantedBytes < uniqueRecvedBytes)
        state.grantedBytes = uniqueRecvedBytes;

    // 只有当真正接收完所有唯一字节时才移除流
    if (uniqueRecvedBytes >= msgSize)
    {
        std::cout << "HomaScheduler: Flow " << flowId << " completed with " << uniqueRecvedBytes
                  << "/" << msgSize << " unique bytes" << std::endl;
        Simulator::Schedule(NanoSeconds(100), &RoCEv2Homa::SendGrantACK, flow, 0, 0x00);
        RemoveFlow(flowId);
    }
}

void
RoCEv2Homa::HomaScheduler::RemoveFlow(uint32_t flowId)
{
    m_activeFlows.erase(flowId);
}

void
RoCEv2Homa::HomaScheduler::AddReadyToSendGrant(uint32_t flowId)
{
    m_readyToSendQueue.push_back(flowId);
}

void
RoCEv2Homa::HomaScheduler::CheckSchedule(uint32_t packetSize)
{
    // Clean up completed flows from m_activeFlows
    for (auto it = m_activeFlows.begin(); it != m_activeFlows.end();)
    {
        if (it->second.recvedBytes >= it->second.msgSize)
        {
            it = m_activeFlows.erase(it);
        }
        else
        {
            ++it;
        }
    }

    // Remove invalid flows from m_readyToSendQueue
    m_readyToSendQueue.erase(std::remove_if(m_readyToSendQueue.begin(),
                                            m_readyToSendQueue.end(),
                                            [this](uint32_t id) {
                                                return m_activeFlows.find(id) ==
                                                       m_activeFlows.end();
                                            }),
                             m_readyToSendQueue.end());

    // SRPT: Sort active ready-to-send grants by remaining bytes
    std::sort(m_readyToSendQueue.begin(), m_readyToSendQueue.end(), [this](uint32_t a, uint32_t b) {
        uint32_t remA = m_activeFlows[a].msgSize - m_activeFlows[a].recvedBytes;
        uint32_t remB = m_activeFlows[b].msgSize - m_activeFlows[b].recvedBytes;
        return remA < remB;
    });
    if(m_readyToSendQueue.size() > m_overcommitLevel) std::cout<<"Have overcommit"<<std::endl;
    // Grant top N
    uint32_t count = 0;
    auto it = m_readyToSendQueue.begin();
    while (it != m_readyToSendQueue.end() && count < m_overcommitLevel)
    {
        uint32_t id = *it;
        FlowState& state = m_activeFlows[id];

        uint32_t grantStep = packetSize; // Grant a packet at a time
        uint32_t newGrant = state.grantedBytes + grantStep;
        if (newGrant > state.msgSize)
            newGrant = state.msgSize;

        if (newGrant >= state.grantedBytes)
        {
            if (count % 2 == 0)
            {
                state.flow->SendGrantACK(grantStep, (uint32_t)0); // 0x00
            }
            else
            {
                // 0x06=1
                std::cout << "send in prio 1" << std::endl;
                state.flow->SendGrantACK(grantStep, (uint32_t)6); // try best to average queue
                                                                  // length
            }
            state.grantedBytes = newGrant;
        }

        // Pop the grant
        it = m_readyToSendQueue.erase(it);
        count++;
    }
    std::cout << "count=" << count << std::endl;
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
