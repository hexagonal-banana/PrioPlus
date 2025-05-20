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

#include "rocev2-socket.h"
#include "rocev2-timely.h"

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
    static TypeId tid =
        TypeId("ns3::RoCEv2Timely")
            .SetParent<RoCEv2CongestionOps>()
            .AddConstructor<RoCEv2Timely>()
            .SetGroupName("Dcb")
            .AddAttribute("Alpha",
                          "Timely's alpha. EWMA weight parameter",
                          DoubleValue(0.875),
                          MakeDoubleAccessor(&RoCEv2Timely::m_alpha),
                          MakeDoubleChecker<double>())
            .AddAttribute("Beta",
                          "Timely's beta, the multiplicative decrement factor",
                          DoubleValue(0.8),
                          MakeDoubleAccessor(&RoCEv2Timely::m_mdFactor),
                          MakeDoubleChecker<double>())
            .AddAttribute("RateAIRatio",
                          "Timely's RateAI ratio (delta in paper)",
                          DoubleValue(0.0005),
                          MakeDoubleAccessor(&RoCEv2Timely::m_raiRatio),
                          MakeDoubleChecker<double>())
            .AddAttribute("HAIMode",
                          "Timely's HAI mode",
                          BooleanValue(true),
                          MakeBooleanAccessor(&RoCEv2Timely::m_haiMode),
                          MakeBooleanChecker())
            .AddAttribute("MaxStage",
                          "Timely's maximum stage (N), used for HAI mode",
                          UintegerValue(5),
                          MakeUintegerAccessor(&RoCEv2Timely::m_maxStage),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("Tlow",
                          "Timely's Tlow",
                          TimeValue(MilliSeconds(5)),
                          MakeTimeAccessor(&RoCEv2Timely::m_tLow),
                          MakeTimeChecker())
            .AddAttribute("Thigh",
                          "Timely's Thigh",
                          TimeValue(MilliSeconds(50)),
                          MakeTimeAccessor(&RoCEv2Timely::m_tHigh),
                          MakeTimeChecker())
            .AddAttribute("UpdateFreqMode",
                          "Timely's update frequency mode: PER_RTT or PER_SEVERAL_PKTS",
                          EnumValue(RoCEv2Timely::UpdateFreq::PER_RTT),
                          MakeEnumAccessor(&RoCEv2Timely::m_updateFreq),
                          MakeEnumChecker(RoCEv2Timely::UpdateFreq::PER_RTT,
                                          "PER_RTT",
                                          RoCEv2Timely::UpdateFreq::PER_SEVERAL_PKTS,
                                          "PER_SEVERAL_PKTS"))
            .AddAttribute("UpdateFreqPacket",
                          "Timely's update frequency in packets",
                          UintegerValue(64),
                          MakeUintegerAccessor(&RoCEv2Timely::m_perpackets),
                          MakeUintegerChecker<uint32_t>());
    return tid;
}

RoCEv2Timely::RoCEv2Timely()
    : RoCEv2CongestionOps(std::make_shared<Stats>()),
      m_stats(std::dynamic_pointer_cast<Stats>(RoCEv2CongestionOps::m_stats))
{
    NS_LOG_FUNCTION(this);
    Init();
}

RoCEv2Timely::RoCEv2Timely(Ptr<RoCEv2SocketState> sockState)
    : RoCEv2CongestionOps(sockState, std::make_shared<Stats>()),
      m_stats(std::dynamic_pointer_cast<Stats>(RoCEv2CongestionOps::m_stats))
{
    NS_LOG_FUNCTION(this);
    Init();
}

RoCEv2Timely::~RoCEv2Timely()
{
    NS_LOG_FUNCTION(this);
}

void
RoCEv2CreditSpraying::SetReady()
{
    NS_LOG_FUNCTION(this);
    // send credit request and set timer for it
    SendCreditRequest(m_sockState->GetBaseOneWayDelay() + m_rttCorrection);
}

void
RoCEv2CreditSpraying::SendCreditRequest(Time rto)
{
    // To stop sending, we set the cwnd to 0
    m_sockState->SetCwnd(0);

    // Check if a Req is just sent
    if (m_cReqTimeOut.IsRunning())
    {
        return;
    }

    NS_ASSERT_MSG(!m_sendCreditReqCb.IsNull(), "SendCreditReqCb not set!");
    // Check if the flow is stopped
    if (CheckStopCondition())
        return;

    // Send a CreditReq packet
    bool success = m_sendCreditReqCb(0);
    if (success)
    {
        // m_inflightProbes[m_probeSeq] = Simulator::Now().GetNanoSeconds();
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
    // Cancel previous probe event
    if (m_cReqTimeOut.IsRunning())
        m_cReqTimeOut.Cancel();
    m_cReqTimeOut = Simulator::Schedule(rto, &RoCEv2CreditSpraying::SendCreditRequest, this);
    NS_LOG_DEBUG(Simulator::Now().GetPicoSeconds()
                 << " " << Simulator::GetContext() << " Schedule probe after "
                 << rto.GetPicoSeconds() << "ps");
}

void
RoCEv2CreditSpraying::SetSendCreditReqCb(Callback<bool, uint32_t> sendCreditReqCb)
{
    m_sendCreditReqCb = sendCreditReqCb;
}

void
RoCEv2CreditSpraying::UpdateStateSend(Ptr<Packet> packet)
{
    NS_LOG_FUNCTION(this << packet);

    // Get packet's PSN from roceheader.
    RoCEv2Header roceHeader;
    packet->PeekHeader(roceHeader);
    // Add current time to the map.
    m_tsMap[roceHeader.GetPSN()] = Simulator::Now();
}

void
RoCEv2CreditSpraying::UpdateStateWithRcvACK(Ptr<Packet> ack,
                                            const RoCEv2Header& roce,
                                            const uint32_t senderNextPSN)
{
    // credit spraying 收到 ACK&Credit 的逻辑
    // 计算 ACK 的包的个数，SetCwnd更新窗口
    NS_LOG_FUNCTION(this << ack << roce);
    uint32_t ackedPkts = std::max((uint32_t)0, roce.GetPSN() - m_sockState->GetPrevFrontPsn());
    m_sockState->SetCwnd(m_sockState->GetCwnd() + 1 - ackedPkts);
}

std::string
RoCEv2Timely::GetName() const
{
    return "Timely";
}

void
RoCEv2Timely::Init()
{
    m_prevRtt = Seconds(-1.0);
    m_incStage = 1;

    RegisterCongestionType(GetTypeId());

    m_nextUpdateSeq = 0;
}

RoCEv2Timely::Stats::Stats()
{
    NS_LOG_FUNCTION(this);
    BooleanValue bv;
    if (GlobalValue::GetValueByNameFailSafe("detailedSenderStats", bv))
        bDetailedSenderStats = bv.Get();
    else
        bDetailedSenderStats = false;
}

void
RoCEv2Timely::Stats::RecordPacketDelay(Time sendTs, Time recvTs, Time delay)
{
    if (bDetailedSenderStats)
    {
        vPacketDelay.push_back(std::make_tuple(sendTs, recvTs, delay));
    }
}

void
RoCEv2Timely::Stats::RecordPacketDelayGradient(Time sendTs, Time recvTs, double gradient)
{
    if (bDetailedSenderStats)
    {
        vPacketDelayGradient.push_back(std::make_tuple(sendTs, recvTs, gradient));
    }
}

} // namespace ns3
