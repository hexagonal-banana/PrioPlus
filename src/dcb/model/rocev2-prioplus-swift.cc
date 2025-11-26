/*
 * Copyright (c) 2023
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
 * Author: Zhaochen Zhang (zhaochen.zhang@outlook.com)
 */

#include "rocev2-prioplus-swift.h"

#include "rocev2-socket.h"

#include "ns3/global-value.h"
#include "ns3/simulator.h"

#include <boost/json.hpp>
#include <cmath>
#include <fstream>
#include <iostream>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("RoCEv2PrioplusSwift");

NS_OBJECT_ENSURE_REGISTERED(RoCEv2PrioplusSwift);

TypeId
RoCEv2PrioplusSwift::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::RoCEv2PrioplusSwift")
            .SetParent<RoCEv2CongestionOps>()
            .AddConstructor<RoCEv2PrioplusSwift>()
            .SetGroupName("Dcb")
            .AddAttribute("TlowBytes",
                          "PrioPlus's Tlow, set in bytes and will be converted to time",
                          StringValue("75KB"),
                          MakeStringAccessor(&RoCEv2PrioplusSwift::SetTlowInBytes),
                          MakeStringChecker())
            .AddAttribute("ThighBytes",
                          "PrioPlus's Thigh, set in bytes and will be converted to time",
                          StringValue("150KB"),
                          MakeStringAccessor(&RoCEv2PrioplusSwift::SetThighInBytes),
                          MakeStringChecker())
            .AddAttribute("RateAIRatio",
                          "Swift's RateAI ratio.",
                          DoubleValue(0.005),
                          MakeDoubleAccessor(&RoCEv2PrioplusSwift::m_raiRatio),
                          MakeDoubleChecker<double>())
            .AddAttribute("Beta",
                          "Swift's beta, the multiplicative decrement factor",
                          DoubleValue(0.8),
                          MakeDoubleAccessor(&RoCEv2PrioplusSwift::m_mdFactor),
                          MakeDoubleChecker<double>())
            .AddAttribute("MaxMdf",
                          "Swift's max md factor.",
                          DoubleValue(0.4),
                          MakeDoubleAccessor(&RoCEv2PrioplusSwift::m_maxMdFactor),
                          MakeDoubleChecker<double>())
            .AddAttribute("RateLinearStart",
                          "PrioPlus's linear start ratio.",
                          DoubleValue(1. / 4.),
                          MakeDoubleAccessor(&RoCEv2PrioplusSwift::m_linearStartRatio),
                          MakeDoubleChecker<double>())
            .AddAttribute("ProbeInterval",
                          "The default interval between two probes, 0 for base RTT.",
                          TimeValue(Time(0)),
                          MakeTimeAccessor(&RoCEv2PrioplusSwift::m_probeInterval),
                          MakeTimeChecker())
            .AddAttribute("IncastAvoidance",
                          "If enabled, the PrioPlus will avoid incast by calculating fair share",
                          BooleanValue(false),
                          MakeBooleanAccessor(&RoCEv2PrioplusSwift::m_incastAvoidance),
                          MakeBooleanChecker())
            .AddAttribute("ForgetFactor",
                          "Forget factor for incast avoidance.",
                          DoubleValue(1.05),
                          MakeDoubleAccessor(&RoCEv2PrioplusSwift::m_forgetFactor),
                          MakeDoubleChecker<double>())
            .AddAttribute("HighPriority",
                          "Is this flow high priority or not",
                          BooleanValue(false),
                          MakeBooleanAccessor(&RoCEv2PrioplusSwift::m_highPriority),
                          MakeBooleanChecker())
            .AddAttribute("DirectStart",
                          "Is this flow skip probe before start or not",
                          BooleanValue(false),
                          MakeBooleanAccessor(&RoCEv2PrioplusSwift::m_directStart),
                          MakeBooleanChecker())
            .AddAttribute("ForgetCountEnabled",
                          "Forget count enabled or not",
                          BooleanValue(false),
                          MakeBooleanAccessor(&RoCEv2PrioplusSwift::m_forgetCountEnabled),
                          MakeBooleanChecker())
            .AddAttribute("RttBased",
                          "Use RTT based or not",
                          BooleanValue(false),
                          MakeBooleanAccessor(&RoCEv2PrioplusSwift::m_rttBased),
                          MakeBooleanChecker())
            .AddAttribute("TlowAiStep",
                          "The AI step when the delay is lower than tlow, 0 for disable.",
                          DoubleValue(0.0),
                          MakeDoubleAccessor(&RoCEv2PrioplusSwift::m_tlowAiStep),
                          MakeDoubleChecker<double>())
            .AddAttribute("DualRttMi",
                          "Use dual RTT for MI or not",
                          BooleanValue(true),
                          MakeBooleanAccessor(&RoCEv2PrioplusSwift::m_dualRttMi),
                          MakeBooleanChecker())
            .AddAttribute("ExceedLimit",
                          "The limit of delay exceed the THigh",
                          UintegerValue(2),
                          MakeUintegerAccessor(&RoCEv2PrioplusSwift::m_exceedLimit),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("DelayErrorCdf",
                          "The file name of delay error cdf",
                          StringValue(""),
                          MakeStringAccessor(&RoCEv2PrioplusSwift::m_delayErrorFile),
                          MakeStringChecker())
            .AddAttribute("DelayErrorScale",
                          "The scale of delay error",
                          DoubleValue(1.0),
                          MakeDoubleAccessor(&RoCEv2PrioplusSwift::m_delayErrorScale),
                          MakeDoubleChecker<double>())
            .AddAttribute("ChannelWidthBytes",
                          "The width of a channel in Bytes",
                          StringValue("0KB"),
                          MakeStringAccessor(&RoCEv2PrioplusSwift::SetChannelWidth),
                          MakeStringChecker())
            .AddAttribute("ChannelIntervalBytes",
                          "The interval of a channel in Bytes",
                          StringValue("0KB"),
                          MakeStringAccessor(&RoCEv2PrioplusSwift::SetChannelInterval),
                          MakeStringChecker())
            .AddAttribute("PriorityNum",
                          "The number of priority in the network",
                          UintegerValue(8),
                          MakeUintegerAccessor(&RoCEv2PrioplusSwift::m_priorityNum),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("PriorityIndex",
                          "The priority index of this flow, larger is higher",
                          UintegerValue(0),
                          MakeUintegerAccessor(&RoCEv2PrioplusSwift::m_priorityIndex),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("PriorityConfig",
                          "The file name of priority config",
                          StringValue(""),
                          MakeStringAccessor(&RoCEv2PrioplusSwift::m_priorityConfigFile),
                          MakeStringChecker())
            .AddAttribute(
                "RttRecordStructure",
                "The structure of RTT record, Deque or Map. Map is for reordering network.",
                EnumValue(RttRecordStructure::MAP),
                MakeEnumAccessor(&RoCEv2PrioplusSwift::m_rttRecordStructure),
                MakeEnumChecker(RttRecordStructure::DEQUE,
                                "Deque",
                                RttRecordStructure::MAP,
                                "Map"));
    ;
    return tid;
}

RoCEv2PrioplusSwift::RoCEv2PrioplusSwift()
    : RoCEv2CongestionOps(std::make_shared<Stats>()),
      m_stats(std::dynamic_pointer_cast<Stats>(RoCEv2CongestionOps::m_stats))
{
    NS_LOG_FUNCTION(this);
    Init();
}

RoCEv2PrioplusSwift::RoCEv2PrioplusSwift(Ptr<RoCEv2SocketState> sockState)
    : RoCEv2CongestionOps(sockState, std::make_shared<Stats>()),
      m_stats(std::dynamic_pointer_cast<Stats>(RoCEv2CongestionOps::m_stats))
{
    NS_LOG_FUNCTION(this);
    Init();
}

RoCEv2PrioplusSwift::~RoCEv2PrioplusSwift()
{
    NS_LOG_FUNCTION(this);
}

void
RoCEv2PrioplusSwift::SetReady()
{
    NS_LOG_FUNCTION(this);

    m_rngDelayError = CreateObject<EmpiricalRandomVariable>();
    // Enable interpolation from CDF
    m_rngDelayError->SetAttribute("Interpolate", BooleanValue(true));

    m_rngProbeTime = CreateObject<UniformRandomVariable>();
    m_rngProbeTime->SetAttribute("Min", DoubleValue(0));
    m_rngProbeTime->SetAttribute("Max", DoubleValue(1));

    // Call base class's SetRateRatio to set the start rate ratio
    RoCEv2CongestionOps::SetRateRatio(m_startRateRatio);

    // Sanity check is in SetChannelThres and SetPriorityConfig
    SetChannelThres();
    SetPriorityConfig();
    ReadDelayErrorCdf();

    if (m_rttBased)
    {
        m_tLowThreshold = m_sockState->GetBaseRtt() + ConvertBytesToTime(m_tLowThresholdInBytes);
        m_tHighThreshold = m_sockState->GetBaseRtt() + ConvertBytesToTime(m_tHighThresholdInBytes);
        m_rttCorrection = Time(0);
    }
    else
    {
        m_tLowThreshold =
            m_sockState->GetBaseOneWayDelay() + ConvertBytesToTime(m_tLowThresholdInBytes);
        m_tHighThreshold =
            m_sockState->GetBaseOneWayDelay() + ConvertBytesToTime(m_tHighThresholdInBytes);
        m_rttCorrection = m_sockState->GetBaseRtt() - m_sockState->GetBaseOneWayDelay();
    }

    m_linearStartBytes = m_sockState->GetBaseBdp() * m_linearStartRatio;
    m_aiBytes = m_sockState->GetBaseBdp() * m_raiRatio;

    // Set incast avoidance rate to 1, which is the max rate as it will be used in min operation
    m_incastAvoidanceRate = 1;

    // If the probe interval is 0, set it to base RTT
    if (m_probeInterval == Time(0))
    {
        m_probeInterval = m_sockState->GetBaseRtt();
    }

    if (m_highPriority)
    {
        // For highest priority, start at line rate and tHigh is set to infinity
        RoCEv2CongestionOps::SetRateRatio(1.0);
        m_tHighThreshold = Time::Max();
    }
    else
    {
        if (m_directStart)
        {
            // For direct start, start at linear rate and skip probe
            RoCEv2CongestionOps::SetRateRatio(m_linearStartRatio);
        }
        // Stop sending in the beginning, no delay is observed so pass base one way delay
        StopSendingAndStartProbe(m_sockState->GetBaseOneWayDelay() + m_rttCorrection);
    }
}

void
RoCEv2PrioplusSwift::UpdateStateSend(Ptr<Packet> packet)
{
    NS_LOG_FUNCTION(this << packet);
    RoCEv2Header roceHeader;
    packet->PeekHeader(roceHeader);
    // Do not need to add a PrioplusHeader into packet, record the sendTs directly
    if (m_rttRecordStructure == RttRecordStructure::DEQUE)
    {
        m_inflightPkts.push_back(std::make_tuple(roceHeader.GetPSN(),
                                                 Simulator::Now().GetNanoSeconds(),
                                                 m_sockState->GetRateRatioPercent()));
    }
    else if (m_rttRecordStructure == RttRecordStructure::MAP)
    {
        m_inflightPktsMap[roceHeader.GetPSN()] =
            std::make_tuple(roceHeader.GetPSN(),
                            Simulator::Now().GetNanoSeconds(),
                            m_sockState->GetRateRatioPercent());
    }
    else
    {
        NS_FATAL_ERROR("Unknown RttRecordStructure");
    }
}

void
RoCEv2PrioplusSwift::UpdateStateWithGenACK(Ptr<Packet> packet, Ptr<Packet> ack)
{
    NS_LOG_FUNCTION(this << packet << ack);
    // Add a PrioplusHeader into ack, which record the ts of recv the data packet.
    // Note that the ACK has RoCEv2Header and AETHeader. 
    // And we should add PrioplusHeader between them.
    RoCEv2Header roceHeader;
    PrioplusHeader PrioplusHeader;
    ack->RemoveHeader(roceHeader);
    ack->AddHeader(PrioplusHeader);
    ack->AddHeader(roceHeader);
}

void
RoCEv2PrioplusSwift::UpdateStateWithRcvACK(Ptr<Packet> ack,
                                          const RoCEv2Header& roce,
                                          const uint32_t senderNextPSN)
{
    NS_LOG_FUNCTION(this << ack << roce);
    // NS_LOG_DEBUG(Simulator::GetContext());

    // State transition below
    uint32_t ackSeq = roce.GetPSN();
    bool rttPass = false;
    if (m_lastUpdateSeq != 0)
    {
        // not first ACK
        rttPass = ackSeq > m_lastUpdateSeq;
        if (rttPass)
        {
            m_lastUpdateSeq = senderNextPSN;
            m_isFirstRtt = false;
            m_secondRtt = !m_secondRtt;
            if (!m_secondRtt || !m_dualRttMi)
            {
                // the rtt after the first rtt, wait for delay
                // If we dont use dual rtt for MI, set miBytes to 0 at the begin of every RTT
                m_miBytes = 0;
            }
            if (m_incastAvoidanceRate != 1)
            {
                m_incastAvoidanceRate = std::min(1., m_incastAvoidanceRate * m_forgetFactor);
            }
        }
    }
    else
    {
        // first ACK
        m_lastUpdateSeq = senderNextPSN;
    }

    // Calculate the delay from the ACK
    if (Simulator::GetContext() == 5 && ackSeq == 15988)
        NS_LOG_DEBUG(Simulator::Now().GetNanoSeconds());
    PrioplusHeader PrioplusHeader;
    ack->RemoveHeader(PrioplusHeader);

    PktInfo info;
    if (m_rttRecordStructure == RttRecordStructure::DEQUE)
    {
        info = m_inflightPkts.front();
        // Pop the first element in m_inflightPkts after using it
        m_inflightPkts.pop_front();
    }
    else if (m_rttRecordStructure == RttRecordStructure::MAP)
    {
        info = m_inflightPktsMap[ackSeq - 1];
    }
    else
    {
        NS_FATAL_ERROR("Unknown RttRecordStructure");
    }
    auto [sendPsn, sendTs, _] = info;

    Time delay;
    if (m_rttBased)
    {
        delay = Simulator::Now() - NanoSeconds(sendTs);
    }
    else
    {
        delay = PrioplusHeader.GetTs() - NanoSeconds(sendTs);
    }
    IncludeDelayError(delay);

    uint32_t curCwnd = m_sockState->GetCwnd();

    if (delay > m_tHighThreshold && ++m_exceedCount >= m_exceedLimit)
    {
        StopSendingAndStartProbe(delay);

        // Check if incast is detected
        if (m_incastAvoidance)
        {
            // Record the cwnd entering the high delay state
            if (curCwnd != 0)
            {
                m_cwndBeforeTHigh = curCwnd;
            }
            // Note that delayGradient is update priodically, may not calculate the correct rate
            uint32_t concurrentFlowNum = 0;
            double concurrentFlowNumFromDelay = 0;

            // there may occur division by zero
            concurrentFlowNumFromDelay =
                ((double)(delay + m_rttCorrection).GetNanoSeconds() /
                 ((double)(m_tLowThreshold + m_rttCorrection).GetNanoSeconds() *
                  ((double)m_cwndBeforeTHigh / m_sockState->GetBaseBdp())));

            concurrentFlowNum = std::min((uint32_t)2000, (uint32_t)concurrentFlowNumFromDelay);
            m_forgetCount = 1. / m_linearStartRatio + 2;

            m_incastAvoidanceRate = std::min(m_incastAvoidanceRate, 1.0 / concurrentFlowNum);
            m_incastAvoidanceTime = Simulator::Now();
        }

        return;
    }
    else if ((!m_rttBased && delay < m_sockState->GetBaseOneWayDelay() + Time("1us")) ||
             (m_rttBased && delay < m_sockState->GetBaseRtt() + Time("1us")))
    {
        if (rttPass)
        {
            if (m_incastAvoidance && m_incastAvoidanceRate != 1)
            {
                curCwnd += m_incastAvoidanceRate * m_linearStartBytes;
                if (m_forgetCount)
                {
                    m_forgetCount -= 1;
                }
                else
                {
                    if (m_forgetCountEnabled)
                        m_incastAvoidanceRate = std::min(1., m_incastAvoidanceRate * 2);
                }
            }
            else
            {
                curCwnd += m_linearStartBytes;
            }
        }
        m_stats->RecordCompleteStats(
            Stats::PrioplusSwiftCompleteStats{Simulator::Now(),
                                             delay,
                                             (uint32_t)m_sockState->GetCwnd(),
                                             m_incastAvoidanceRate,
                                             0,
                                             0,
                                             0});
    }
    else
    {
        DoRateUpdate(0, delay, 0, rttPass && (m_secondRtt || !m_dualRttMi), curCwnd);
    }

    // Set exceed count to 0 if delay is lower than THigh
    if (delay < m_tHighThreshold)
        m_exceedCount = 0;

    if (curCwnd != 0 && m_probeEvent.IsRunning())
    {
        m_probeEvent.Cancel();
    }

    SetCwnd(curCwnd);
}

void
RoCEv2PrioplusSwift::UpdateStateWithRto()
{
    // If in probe state, send a probe packet now
    // Check probe state by cwnd
    if (m_sockState->GetCwnd() == 0)
    {
        ScheduleProbePacket(m_sockState->GetBaseOneWayDelay() + m_rttCorrection);
    }
}

void
RoCEv2PrioplusSwift::DoRateUpdate(double refenceRate,
                                 Time delay,
                                 double scaleFactor,
                                 bool shouldAi,
                                 uint32_t& curCwnd)
{
    // Update curRateRatio to make current rate approach the line rate
    // Mimic the control in swift

    Time targetDelay = m_tLowThreshold;

    uint64_t totalPacketSize = m_sockState->GetPacketSize();
    if (delay < targetDelay)
    {
        // Do AI, cwndPackets is cwnd in packets

        double cwndPackets;
        if (m_isLimiting)
        {
            cwndPackets = ((static_cast<double>(m_sockState->GetCwnd()) + totalPacketSize - 1.0) /
                           totalPacketSize);
        }
        else
        {
            cwndPackets = (m_sockState->GetRateRatioPercent() * m_sockState->GetBaseBdp() +
                           totalPacketSize - 1.0) /
                          totalPacketSize;
        }
        if (cwndPackets < 1)
        {
            cwndPackets = 1.0;
        }

        double aiScaleFactor = m_incastAvoidanceRate > 0.2 ? 1 : m_incastAvoidanceRate * 5;

        curCwnd = curCwnd + m_aiBytes / cwndPackets * aiScaleFactor + m_miBytes / cwndPackets;
        m_stats->RecordCompleteStats(
            Stats::PrioplusSwiftCompleteStats{Simulator::Now(),
                                             delay,
                                             (uint32_t)m_sockState->GetCwnd(),
                                             m_incastAvoidanceRate,
                                             m_aiBytes / cwndPackets * aiScaleFactor,
                                             m_miBytes / cwndPackets,
                                             0});

        // for fast start
        if (shouldAi)
        {
            [[maybe_unused]] uint32_t linearPart =
                m_linearStartBytes * m_incastAvoidanceRate *
                ((double)(m_tLowThreshold - delay).GetNanoSeconds() /
                 (m_tLowThreshold - m_sockState->GetBaseOneWayDelay()).GetNanoSeconds());
            [[maybe_unused]] uint32_t miPart = ((double)(m_tLowThreshold - delay).GetNanoSeconds() /
                                                (delay + m_rttCorrection).GetNanoSeconds()) *
                                               curCwnd;
            // For ablation study, if m_tlowAiStep is set, use it to replace miPart
            if (m_tlowAiStep)
                miPart = m_tlowAiStep * m_sockState->GetBaseBdp();
            m_miBytes = std::min(curCwnd / 2, miPart);
        }
    }
    else if (Simulator::Now() - m_tLastDecrease > m_sockState->GetBaseRtt())
    {
        // Do MD
        double factor =
            std::min(m_mdFactor * (delay.GetNanoSeconds() - targetDelay.GetNanoSeconds()) /
                         delay.GetNanoSeconds(),
                     m_maxMdFactor);
        curCwnd = curCwnd * (1.0 - factor);
        // Schedule a timer to set m_canDecrease to true.
        m_tLastDecrease = Simulator::Now();

        m_stats->RecordCompleteStats(
            Stats::PrioplusSwiftCompleteStats{Simulator::Now(),
                                             delay,
                                             (uint32_t)m_sockState->GetCwnd(),
                                             m_incastAvoidanceRate,
                                             0,
                                             0,
                                             factor});
    }
}

void
RoCEv2PrioplusSwift::SetCwnd(uint32_t cwnd)
{
    // if cwnd is too low, stop sending and start probe
    if (cwnd <= m_sockState->GetPacketSize() * 0.1)
    {
        StopSendingAndStartProbe(m_sockState->GetBaseOneWayDelay() + m_rttCorrection);
        return;
    }
    // cwnd must small than tlow in Bytes
    uint32_t maxCwnd = m_tLowThresholdInBytes.GetValue() + m_sockState->GetBaseBdp();
    m_sockState->SetCwnd(std::min(cwnd, maxCwnd));
    // Calculate the rate ratio based on the cwnd
    if (m_isPacing)
    {
        double rateRatio = static_cast<double>(cwnd) / m_sockState->GetBaseBdp();
        m_sockState->SetRateRatioPercent(rateRatio);
    }
}

void
RoCEv2PrioplusSwift::ResetRttRecord()
{
    m_lastUpdateSeq = m_sockState->GetTxBuffer()->GetFrontPsn();
}

void
RoCEv2PrioplusSwift::StopSendingAndStartProbe(Time delay)
{
    // To stop sending, we set the cwnd to 0
    m_sockState->SetCwnd(0);

    m_stats->RecordCompleteStats(Stats::PrioplusSwiftCompleteStats{Simulator::Now(),
                                                                  delay,
                                                                  0,
                                                                  m_incastAvoidanceRate,
                                                                  0,
                                                                  0,
                                                                  0});

    // Start probe
    ScheduleProbePacket(delay);
}

void
RoCEv2PrioplusSwift::SendProbePacket()
{
    // Check if a probe is just sent
    if (m_probeEvent.IsRunning())
    {
        return;
    }

    NS_ASSERT_MSG(!m_sendProbeCb.IsNull(), "SendProbeCb not set!");
    // Check if the flow is stopped
    if (CheckStopCondition())
        return;

    // Send a probe packet
    bool success = m_sendProbeCb(m_probeSeq);
    if (success)
    {
        m_inflightProbes[m_probeSeq] = Simulator::Now().GetNanoSeconds();
        m_probeSeq += 1;
        // Log the time and seq of the probe
        NS_LOG_DEBUG(Simulator::Now().GetNanoSeconds() << " Send probe " << m_probeSeq - 1);
    }
    else
    {
        NS_LOG_WARN("Send probe failed!");
    }
}

void
RoCEv2PrioplusSwift::ScheduleProbePacket(Time delay)
{
    Time qDelay = delay;
    // If qDelay is larger than m_tLowThreshold, minus m_tLowThreshold
    if (qDelay > m_tLowThreshold)
    {
        qDelay -= m_tLowThreshold;
    }
    else
    {
        qDelay -= (m_sockState->GetBaseOneWayDelay() + m_rttCorrection);
    }
    Time randomDelay = // Time(0);
        NanoSeconds(m_rngProbeTime->GetValue() * m_probeInterval.GetNanoSeconds());
    // Cancel previous probe event
    if (m_probeEvent.IsRunning())
        m_probeEvent.Cancel();
    m_probeEvent =
        Simulator::Schedule(qDelay + randomDelay, &RoCEv2PrioplusSwift::SendProbePacket, this);
    NS_LOG_DEBUG(Simulator::Now().GetPicoSeconds()
                 << " " << Simulator::GetContext() << " Schedule probe after "
                 << (qDelay + randomDelay).GetPicoSeconds() << "ps");
}

void
RoCEv2PrioplusSwift::UpdateStateWithRecvProbeAck(Ptr<Packet> probe,
                                                const RoCEv2Header& roce,
                                                uint32_t senderNextPSN)
{
    uint32_t ackSeq = roce.GetPSN();
    // Calculate the delay from the ACK
    PrioplusHeader PrioplusHeader;
    probe->RemoveHeader(PrioplusHeader);
    // Pop the first element in m_inflightPkts after using it
    uint64_t sendTs = m_inflightProbes[ackSeq];

    // Here we assume no packet loss or reordering in the network
    Time delay;
    if (m_rttBased)
    {
        delay = Simulator::Now() - NanoSeconds(sendTs);
    }
    else
    {
        delay = PrioplusHeader.GetTs() - NanoSeconds(sendTs);
    }

    if (Simulator::Now().GetNanoSeconds() > 104144771)
        NS_LOG_DEBUG(Simulator::Now().GetNanoSeconds()
                     << " " << Simulator::GetContext()
                     << " Probe Delay: " << delay.GetNanoSeconds());

    if (delay > m_tHighThreshold)
    {
        // Delay too high, schedule next probe after queueing delay + random delay
        ScheduleProbePacket(delay);
    }
    else if ((!m_rttBased && delay < m_sockState->GetBaseOneWayDelay() + Time("1us")) ||
             (m_rttBased && delay < m_sockState->GetBaseRtt() + Time("1us")))
    {
        // Delay is just base one way delay, linear start

        // Cancel the probe event
        m_probeEvent.Cancel();

        // Reset the rtt record
        m_lastUpdateSeq = 0;
        // The first time of flow start, used to determine incast
        m_isFirstRtt = true;
        m_secondRtt = false;
        // m_incastAvoidanceRate = 1;

        // If in incast avoidance mode, set rate ratio to m_incastAvoidanceRate
        if (m_incastAvoidance && m_incastAvoidanceRate != 1)
        {
            if (m_forgetCount)
            {
                m_forgetCount -= 1;
            }
            else
            {
                if (m_forgetCountEnabled)
                    m_incastAvoidanceRate = std::min(1., m_incastAvoidanceRate * 2);
            }
            SetCwnd(m_incastAvoidanceRate * m_linearStartBytes);
        }
        else
        {
            SetCwnd(m_linearStartBytes);
        }
        // Start send data packet now
        m_sendPendingDataCb();

        m_stats->RecordCompleteStats(
            Stats::PrioplusSwiftCompleteStats{Simulator::Now(),
                                             delay,
                                             (uint32_t)m_sockState->GetCwnd(),
                                             m_incastAvoidanceRate,
                                             0,
                                             0,
                                             0});
    }
    else
    {
        // Delay is moderate, should follow, set cwnd to one packet
        SetCwnd(m_sockState->GetPacketSize() / 2);
        m_sendPendingDataCb();

        // Cancel the probe event
        m_probeEvent.Cancel();

        // Reset the rtt record
        m_lastUpdateSeq = 0;
        m_secondRtt = false;

        m_stats->RecordCompleteStats(
            Stats::PrioplusSwiftCompleteStats{Simulator::Now(),
                                             delay,
                                             (uint32_t)m_sockState->GetCwnd(),
                                             m_incastAvoidanceRate,
                                             0,
                                             0,
                                             0});
    }
}

std::string
RoCEv2PrioplusSwift::GetName() const
{
    return "PrioPlusSwift";
}

void
RoCEv2PrioplusSwift::SetTlowInBytes(StringValue tlow)
{
    m_tLowThresholdInBytes = QueueSize(tlow.Get());
}

void
RoCEv2PrioplusSwift::SetThighInBytes(StringValue thigh)
{
    m_tHighThresholdInBytes = QueueSize(thigh.Get());
}

Time
RoCEv2PrioplusSwift::ConvertBytesToTime(QueueSize bytes)
{
    return Time::FromDouble(bytes.GetValue() * 8. / m_sockState->GetDeviceRate()->GetBitRate(),
                            Time::S);
}

void
RoCEv2PrioplusSwift::Init()
{
    PrioplusHeader hd;
    m_extraAckSize += hd.GetSerializedSize(); // ACK has extra PrioplusHeader

    m_tLastDelay = Time(0);
    m_tLastPktSendTime = Time(0);
    m_lastUpdateSeq = 0;
    m_probeSeq = 0;

    m_tLastDecrease = Time(0);
    m_secondRtt = false;
    m_miBytes = 0;

    m_exceedCount = 0;
    m_delayErrorEnabled = false;

    RegisterCongestionType(GetTypeId());
}

void
RoCEv2PrioplusSwift::ReadDelayErrorCdf()
{
    if (m_delayErrorFile.empty())
    {
        m_delayErrorEnabled = false;
        return;
    }
    m_delayErrorEnabled = true;

    std::ifstream delayErrorCdfFile;
    delayErrorCdfFile.open(m_delayErrorFile);

    std::string line;
    std::istringstream lineBuffer;

    std::map<double, int> msgSizeCDF;
    double prob;
    double error;
    while (getline(delayErrorCdfFile, line))
    {
        lineBuffer.clear();
        lineBuffer.str(line);
        lineBuffer >> error;
        lineBuffer >> prob;

        m_rngDelayError->CDF(error * m_delayErrorScale, prob);
    }
    delayErrorCdfFile.close();
}

void
RoCEv2PrioplusSwift::IncludeDelayError(Time& delay)
{
    // First check if the delay error cdf is set
    if (!m_delayErrorEnabled)
        return;
    // Select a random delay error
    double rndValue = m_rngDelayError->GetValue();
    delay += NanoSeconds(rndValue);
}

void
RoCEv2PrioplusSwift::SetChannelWidth(StringValue width)
{
    m_tChannelWidthBytes = QueueSize(width.Get());
}

void
RoCEv2PrioplusSwift::SetChannelInterval(StringValue interval)
{
    m_tChannelIntervalBytes = QueueSize(interval.Get());
}

void
RoCEv2PrioplusSwift::SetChannelThres()
{
    if (m_tChannelWidthBytes.GetValue() == 0 && m_tChannelIntervalBytes.GetValue() == 0)
    {
        // Do not set the channel threshold in this function
        return;
    }
    // Calculate the tlow and thigh as channel's lower and upper bound
    m_tLowThresholdInBytes =
        QueueSize(BYTES,
                  (m_tChannelWidthBytes.GetValue() + m_tChannelIntervalBytes.GetValue()) *
                      (m_priorityNum - m_priorityIndex));
    m_tHighThresholdInBytes =
        QueueSize(BYTES, m_tLowThresholdInBytes.GetValue() + m_tChannelWidthBytes.GetValue());
}

void
RoCEv2PrioplusSwift::SetPriorityConfig()
{
    if (m_priorityConfigFile.empty())
    {
        return;
    }

    std::ifstream configf;
    configf.open(m_priorityConfigFile);
    std::stringstream buf;
    buf << configf.rdbuf();
    boost::system::error_code ec;
    boost::json::object configJsonObj = boost::json::parse(buf.str()).as_object();
    if (ec.failed())
    {
        std::cout << ec.message() << std::endl;
        NS_FATAL_ERROR("Config file read error!");
    }

    // Read ratios from config file
    double highPriorityRatio = configJsonObj["HighPriorityRatio"].as_double();
    double directStartRatio = configJsonObj["DirectStartRatio"].as_double();
    // Check if this is a high priority flow, round to integer
    m_highPriority = m_priorityIndex < (uint32_t)(m_priorityNum * highPriorityRatio + 0.5) || m_priorityIndex == 0;
    // Check if this flow should start directly
    m_directStart = m_priorityIndex < (uint32_t)(m_priorityNum * directStartRatio + 0.5) || m_priorityIndex == 0;

    // Read the linear start ratio array, <linear start ratio, priority ratio>
    std::vector<std::pair<double, double>> linearStartRatioArray;
    for (auto& [key, value] : configJsonObj["LinearStartRateRatio"].as_object())
    {
        linearStartRatioArray.push_back(std::make_pair(std::stod(std::string(key)), value.as_double()));
    }
    // Set the linear start ratio accoding priority ratio
    for (auto& [linearStartRatio, priorityRatio] : linearStartRatioArray)
    {
        if (m_priorityIndex < (uint32_t)(m_priorityNum * priorityRatio + 0.5))
        {
            m_linearStartRatio = linearStartRatio;
            // Set the start rate ratio to linear start ratio
            RoCEv2CongestionOps::SetRateRatio(linearStartRatio);
            break;
        }
    }
}

std::shared_ptr<RoCEv2CongestionOps::Stats>
RoCEv2PrioplusSwift::GetStats() const
{
    return m_stats;
}

void
RoCEv2PrioplusSwift::SetSendProbeCb(Callback<bool, uint32_t> sendProbeCb)
{
    m_sendProbeCb = sendProbeCb;
}

void
RoCEv2PrioplusSwift::SetSendPendingDataCb(Callback<void> sendCb)
{
    m_sendPendingDataCb = sendCb;
}

RoCEv2PrioplusSwift::Stats::Stats()
{
    NS_LOG_FUNCTION(this);
    BooleanValue bv;
    if (GlobalValue::GetValueByNameFailSafe("detailedSenderStats", bv))
        bDetailedSenderStats = bv.Get();
    else
        bDetailedSenderStats = false;
}

void
RoCEv2PrioplusSwift::Stats::RecordCompleteStats(
    RoCEv2PrioplusSwift::Stats::PrioplusSwiftCompleteStats&& stats)
{
    if (bDetailedSenderStats)
    {
        vPrioplusCompleteStats.push_back(stats);
    }
}

} // namespace ns3
