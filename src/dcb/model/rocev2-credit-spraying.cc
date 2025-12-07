/*
 * Copyright (c) 2008 INRIA
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: F.Y. Xue <xue.fyang@foxmail.com>
 */

#include "rocev2-credit-spraying.h"

#include "rocev2-l4-protocol.h"
#include "rocev2-socket.h"

#include "ns3/global-value.h"
#include "ns3/seq-ts-header.h"
#include "ns3/simulator.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("RoCEv2CreditSpraying");

NS_OBJECT_ENSURE_REGISTERED(RoCEv2CreditSpraying);

TypeId
RoCEv2CreditSpraying::GetTypeId()
{
    static TypeId tid = TypeId("ns3::RoCEv2CreditSpraying")
                            .SetParent<RoCEv2CongestionOps>()
                            .AddConstructor<RoCEv2CreditSpraying>()
                            .SetGroupName("Dcb")
                            .AddAttribute("CreditRateRatio",
                                          "Ratio for credit ACK sending rate (0~1).",
                                          DoubleValue(0.1),
                                          MakeDoubleAccessor(&RoCEv2CreditSpraying::m_creditRateRatio),
                                          MakeDoubleChecker<double>(0.0, 1.0));
    return tid;
}

RoCEv2CreditSpraying::RoCEv2CreditSpraying()
    : RoCEv2CongestionOps(std::make_shared<Stats>()),
      m_stats(std::dynamic_pointer_cast<Stats>(RoCEv2CongestionOps::m_stats))
{
    NS_LOG_FUNCTION(this);
    Init();
}

RoCEv2CreditSpraying::RoCEv2CreditSpraying(Ptr<RoCEv2SocketState> sockState)
    : RoCEv2CongestionOps(sockState, std::make_shared<Stats>()),
      m_stats(std::dynamic_pointer_cast<Stats>(RoCEv2CongestionOps::m_stats))
{
    NS_LOG_FUNCTION(this);
    Init();
}

RoCEv2CreditSpraying::~RoCEv2CreditSpraying()
{
    NS_LOG_FUNCTION(this);
}

void
RoCEv2CreditSpraying::SetReady()
{
    NS_LOG_FUNCTION(this);
    // send credit request and set timer for it
    SendCreditRequest(m_sockState->GetBaseOneWayDelay() * 10);  // Magic number for now
}

void
RoCEv2CreditSpraying::SendCreditRequest(Time rto)
{
    NS_LOG_FUNCTION(this << rto);
    // To stop sending, we set the cwnd to 0
    m_sockState->SetCwnd(0);

    // Check if a Req is just sent
    if (m_cReqTimeOut.IsRunning())
    {
        return;
    }

    NS_ASSERT_MSG(!m_sendOutbandPktCb.IsNull(), "SendOutbandPktCb not set!");
    // Check if the flow is stopped
    if (CheckStopCondition())
    {
        return;
    }

    CongestionTypeTag ctTag(GetTypeId().GetUid());
    CreditRequestTag crTag(true);
    std::vector<std::reference_wrapper<const Tag>> packetTags{ctTag, crTag};

    // Send out-of-band credit request packet
    bool success = m_sendOutbandPktCb(0, true, packetTags);
    if (success)
    {
        // m_probeSeq += 1;
        // // Log the time and seq of the probe
        NS_LOG_DEBUG(Simulator::Now().GetNanoSeconds() << " Send Credit Req ");
    }
    else
    {
        NS_LOG_WARN("Send Credit Req failed!");
    }
    // Start probe
    ScheduleNextCreditReq(rto);
}

void
RoCEv2CreditSpraying::ScheduleNextCreditReq(Time rto)
{
    NS_LOG_FUNCTION(this << rto);
    // Cancel previous probe event
    if (m_cReqTimeOut.IsRunning())
    {
        m_cReqTimeOut.Cancel();
    }
    m_cReqTimeOut = Simulator::Schedule(rto, &RoCEv2CreditSpraying::SendCreditRequest, this, rto);
    NS_LOG_DEBUG(Simulator::Now().GetPicoSeconds()
                 << " " << Simulator::GetContext() << " Schedule probe after "
                 << rto.GetPicoSeconds() << "ps");
}

void
RoCEv2CreditSpraying::UpdateStateSend(Ptr<Packet> packet)
{
    NS_LOG_FUNCTION(this << packet);

    // Get packet's PSN from roceheader.
    RoCEv2Header roceHeader;
    packet->PeekHeader(roceHeader);
}

void
RoCEv2CreditSpraying::UpdateStateWithOutbandPkt(Ptr<Packet> packet,
                                                const RoCEv2Header& roce,
                                                const uint32_t senderNextPSN)
{
    NS_LOG_FUNCTION(this << packet << roce << senderNextPSN);
    CreditRequestTag crTag;
    if (!packet->RemovePacketTag(crTag))
    {
        return;
    }

    if (crTag.IsRequest())
    {
        StartCreditAckLoop(roce);
    }
    else
    {
        // Stop credit ACK loop when receiving stop signal
        if (m_creditAckEvent.IsRunning())
        {
            m_creditAckEvent.Cancel();
        }
    }
}

void
RoCEv2CreditSpraying::UpdateStateWithRcvACK(Ptr<Packet> ack,
                                            const RoCEv2Header& roce,
                                            const uint32_t senderNextPSN)
{
    // credit spraying 收到 ACK&Credit 的逻辑
    // 计算 ACK 的包的个数，SetCwnd更新窗口
    NS_LOG_FUNCTION(this << ack << roce << senderNextPSN);
    uint32_t ackedPkts =
        std::max((uint32_t)0, roce.GetPSN() - m_sockState->GetTxBuffer()->GetFrontPsn());
    m_sockState->SetCwnd(m_sockState->GetCwnd() + (1 - ackedPkts) * m_sockState->GetPacketSize());
    m_sendPendingDataCb(); // Trigger sending pending data packets

    // Stop sending further credit requests once any ACK is received
    if (m_cReqTimeOut.IsRunning())
    {
        m_cReqTimeOut.Cancel();
    }

    // If all data are acknowledged, send a stop-credit message once
    if (!m_stopCreditAckSent && roce.GetPSN() == m_sockState->GetTxBuffer()->GetEndPsn())
    {
        m_stopCreditAckSent = true;
        CongestionTypeTag ctTag(GetTypeId().GetUid());
        CreditRequestTag crTag(false);
        std::vector<std::reference_wrapper<const Tag>> packetTags{ctTag, crTag};
        bool success = m_sendOutbandPktCb(m_sockState->GetTxBuffer()->GetEndPsn(), true, packetTags);
        if (!success)
        {
            NS_LOG_WARN("Send stop Credit ACK signal failed!");
        }
    }
}

std::string
RoCEv2CreditSpraying::GetName() const
{
    return "CreditSpraying";
}

void
RoCEv2CreditSpraying::Init()
{
    RegisterCongestionType(GetTypeId());
}

void
RoCEv2CreditSpraying::StartCreditAckLoop(const RoCEv2Header& roce)
{
    NS_LOG_FUNCTION(this << roce);
    if (m_creditAckEvent.IsRunning())
    {
        m_creditAckEvent.Cancel();
    }

    // Compute interval based on current rate/size
    // Ptr<Packet> ack = RoCEv2L4Protocol::GenerateACK(roce.GetDestQP(),
    //                                                 roce.GetSrcQP(),
    //                                                 roce.GetPSN());
    // uint32_t ackBytes = ack->GetSize();
    uint32_t ackBytes = 64; // XXX Magic number for now
    m_creditAckInterval = ComputeCreditAckInterval(ackBytes);

    // Kick off immediately
    SendCreditAck(roce.GetPSN());
}

Time
RoCEv2CreditSpraying::ComputeCreditAckInterval(uint32_t ackBytes) const
{
    NS_LOG_FUNCTION(this << ackBytes);
    // Guard against invalid state
    if (m_creditRateRatio <= 0.0 || m_creditRateRatio > 1.0)
    {
        return Time(0);
    }

    if (m_sockState->GetDeviceRate() == nullptr || m_sockState->GetPacketSize() == 0 || ackBytes == 0)
    {
        return Time(0);
    }

    double lineRate = static_cast<double>(m_sockState->GetDeviceRate()->GetBitRate()); // bits/s
    double dataBytes = static_cast<double>(m_sockState->GetPacketSize());
    double ackBytesD = static_cast<double>(ackBytes);

    double sendRateBits = m_creditRateRatio * lineRate * (ackBytesD / dataBytes); // bits/s
    if (sendRateBits <= 0.0)
    {
        return Time(0);
    }

    double intervalSeconds = (ackBytesD * 8.0) / sendRateBits;
    return Seconds(intervalSeconds);
}

void
RoCEv2CreditSpraying::SendCreditAck(uint32_t psn)
{
    NS_LOG_FUNCTION(this << psn);
    if (CheckStopCondition())
    {
        return;
    }

    CongestionTypeTag ctTag(GetTypeId().GetUid());
    std::vector<std::reference_wrapper<const Tag>> packetTags{ctTag};
    // CreditRequestTag crTag(false);
    // std::vector<std::reference_wrapper<const Tag>> packetTags{ctTag, crTag};

    // Send out-of-band credit ACK packet
    bool success = m_sendOutbandPktCb(psn, false, packetTags);
    if (!success)
    {
        NS_LOG_WARN("Send Credit ACK failed!");
        return;
    }

    if (m_creditAckInterval.IsStrictlyPositive())
    {
        m_creditAckEvent =
            Simulator::Schedule(m_creditAckInterval, &RoCEv2CreditSpraying::SendCreditAck, this, psn);
    }
}

RoCEv2CreditSpraying::Stats::Stats()
{
    NS_LOG_FUNCTION(this);
    BooleanValue bv;
    if (GlobalValue::GetValueByNameFailSafe("detailedSenderStats", bv))
    {
        bDetailedSenderStats = bv.Get();
    }
    else
    {
        bDetailedSenderStats = false;
    }
}

} // namespace ns3
