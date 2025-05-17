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

#include "rocev2-timely.h"

#include "rocev2-socket.h"

#include "ns3/global-value.h"
#include "ns3/seq-ts-header.h"
#include "ns3/simulator.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("RoCEv2Timely");

NS_OBJECT_ENSURE_REGISTERED(RoCEv2Timely);

TypeId
RoCEv2Timely::GetTypeId()
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
RoCEv2Timely::SetReady()
{
    NS_LOG_FUNCTION(this);
    // Reload any config before starting
    SetRateRatio(m_startRateRatio);
    m_minRtt = m_sockState->GetBaseRtt();
}

void
RoCEv2Timely::UpdateStateSend(Ptr<Packet> packet)
{
    NS_LOG_FUNCTION(this << packet);

    // Get packet's PSN from roceheader.
    RoCEv2Header roceHeader;
    packet->PeekHeader(roceHeader);
    // Add current time to the map.
    m_tsMap[roceHeader.GetPSN()] = Simulator::Now();
}

void
RoCEv2Timely::UpdateStateWithRcvACK(Ptr<Packet> ack,
                                    const RoCEv2Header& roce,
                                    const uint32_t senderNextPSN)
{
    NS_LOG_FUNCTION(this << ack << roce);

    uint32_t ackSeq = roce.GetPSN();

    // Read the ACK's PSN and get the corresponding timeslot.
    Time newRtt = Simulator::Now() - m_tsMap[roce.GetPSN() - 1];
    // Record the packet delay for every packet to calculate the orcal gradient
    m_stats->RecordPacketDelay(m_tsMap[roce.GetPSN() - 1], Simulator::Now(), newRtt);

    if (ackSeq < m_nextUpdateSeq)
    {
        return;
    }
    if (m_updateFreq == UpdateFreq::PER_RTT)
    {
        m_nextUpdateSeq = senderNextPSN;
    }
    else if (m_updateFreq == UpdateFreq::PER_SEVERAL_PKTS)
    {
        m_nextUpdateSeq += m_perpackets;
    }
    else
    {
        NS_ASSERT_MSG(false, "Unknown update frequency");
    }

    if (m_prevRtt < Time(0))
    {
        // first ACK, do nothing
    }
    else
    {
        if (Simulator::Now().GetNanoSeconds() > 100250000)
        {
            NS_LOG_DEBUG("newRtt: " << newRtt.GetNanoSeconds()
                                    << ", m_prevRtt: " << m_prevRtt.GetNanoSeconds());
        }
        Time newRttDiff = newRtt - m_prevRtt;
        // update m_rttDiff
        m_rttDiff = m_alpha * newRttDiff + (1 - m_alpha) * m_rttDiff;
        // m_rttDiff divided by m_minRtt to get normalized gradient
        double gradient = m_rttDiff.GetSeconds() / m_minRtt.GetSeconds();

        m_stats->RecordPacketDelayGradient(m_tsMap[roce.GetPSN() - 1], Simulator::Now(), gradient);

        double curRateRatio = m_sockState->GetRateRatioPercent();

        // if newRtt is less than Tlow, additive increment
        if (newRtt < m_tLow)
        {
            curRateRatio += m_raiRatio;
        }
        // if newRtt is greater than Thigh, multiplicative decrement
        else if (newRtt > m_tHigh)
        {
            curRateRatio *= (1.0 - m_mdFactor * (m_tHigh.GetSeconds() / newRtt.GetSeconds()));
        }
        else
        {
            // if gradient is not positive, AI or HAI
            if (gradient <= 0)
            {
                if (m_haiMode)
                    m_incStage++;
                if (m_incStage > m_maxStage)
                {
                    curRateRatio += m_maxStage * m_raiRatio;
                    m_incStage = m_maxStage;
                }
                else
                {
                    // not HAI mode, or HAI mode but not reach max stage
                    curRateRatio += m_raiRatio;
                }
            }
            else
            {
                m_incStage = 1;
                curRateRatio *= (1.0 - m_mdFactor * gradient);
            }
        }
        SetRateRatio(curRateRatio);
        // m_sockState->SetRateRatioPercent(m_curRateRatio);
    }
    // update m_prevRtt
    m_prevRtt = newRtt;
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
