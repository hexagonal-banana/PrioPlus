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

#include "rocev2-prioplus-ledbat.h"

#include "rocev2-prioplus-swift.h"
#include "rocev2-socket.h"

#include "ns3/global-value.h"
#include "ns3/simulator.h"

#include <cmath>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("RoCEv2PrioplusLedbat");

NS_OBJECT_ENSURE_REGISTERED(RoCEv2PrioplusLedbat);

TypeId
RoCEv2PrioplusLedbat::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::RoCEv2PrioplusLedbat")
            .SetParent<RoCEv2CongestionOps>()
            .AddConstructor<RoCEv2PrioplusLedbat>()
            .SetGroupName("Dcb")
            .AddAttribute("TlowBytes",
                          "Prioplus's Tlow, set in bytes and will be converted to time",
                          StringValue("75KB"),
                          MakeStringAccessor(&RoCEv2PrioplusLedbat::SetTlowInBytes),
                          MakeStringChecker())
            .AddAttribute("ThighBytes",
                          "Prioplus's Thigh, set in bytes and will be converted to time",
                          StringValue("150KB"),
                          MakeStringAccessor(&RoCEv2PrioplusLedbat::SetThighInBytes),
                          MakeStringChecker())
            .AddAttribute("RateAIRatio",
                          "Prioplus's RateAI ratio.",
                          DoubleValue(0.005),
                          MakeDoubleAccessor(&RoCEv2PrioplusLedbat::m_raiRatio),
                          MakeDoubleChecker<double>())
            .AddAttribute("RateLinearStart",
                          "Prioplus's linear start ratio.",
                          DoubleValue(1. / 4.),
                          MakeDoubleAccessor(&RoCEv2PrioplusLedbat::m_linearStartRatio),
                          MakeDoubleChecker<double>())
            .AddAttribute("ProbeInterval",
                          "The default interval between two probes, 0 for base RTT.",
                          TimeValue(Time(0)),
                          MakeTimeAccessor(&RoCEv2PrioplusLedbat::m_probeInterval),
                          MakeTimeChecker())
            .AddAttribute("IncastAvoidance",
                          "If enabled, the Prioplus will avoid incast by calculating fair share",
                          BooleanValue(false),
                          MakeBooleanAccessor(&RoCEv2PrioplusLedbat::m_incastAvoidance),
                          MakeBooleanChecker())
            .AddAttribute("ForgetFactor",
                          "Forget factor for incast avoidance.",
                          DoubleValue(1.05),
                          MakeDoubleAccessor(&RoCEv2PrioplusLedbat::m_forgetFactor),
                          MakeDoubleChecker<double>())
            .AddAttribute("Weighted",
                          "Weighted share the bandwidth or not",
                          BooleanValue(false),
                          MakeBooleanAccessor(&RoCEv2PrioplusLedbat::m_weighted),
                          MakeBooleanChecker())
            .AddAttribute("HighPriority",
                          "Is this flow high priority or not",
                          BooleanValue(false),
                          MakeBooleanAccessor(&RoCEv2PrioplusLedbat::m_highPriority),
                          MakeBooleanChecker())
            .AddAttribute("DirectStart",
                          "Is this flow skip probe before start or not",
                          BooleanValue(false),
                          MakeBooleanAccessor(&RoCEv2PrioplusLedbat::m_directStart),
                          MakeBooleanChecker())
            .AddAttribute("ForgetCountEnabled",
                          "Forget count enabled or not",
                          BooleanValue(false),
                          MakeBooleanAccessor(&RoCEv2PrioplusLedbat::m_forgetCountEnabled),
                          MakeBooleanChecker())
            .AddAttribute("RttBased",
                          "Use RTT based or not",
                          BooleanValue(false),
                          MakeBooleanAccessor(&RoCEv2PrioplusLedbat::m_rttBased),
                          MakeBooleanChecker())
            .AddAttribute("NoiseFilterLen",
                          "Number of Current delay samples",
                          UintegerValue(4),
                          MakeUintegerAccessor(&RoCEv2PrioplusLedbat::m_noiseFilterLen),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("Gain",
                          "Offset Gain",
                          DoubleValue(1.0),
                          MakeDoubleAccessor(&RoCEv2PrioplusLedbat::m_gain),
                          MakeDoubleChecker<double>());
    ;
    return tid;
}

RoCEv2PrioplusLedbat::RoCEv2PrioplusLedbat()
    : RoCEv2CongestionOps(std::make_shared<Stats>()),
      m_stats(std::dynamic_pointer_cast<Stats>(RoCEv2CongestionOps::m_stats))
{
    NS_LOG_FUNCTION(this);
    Init();
}

RoCEv2PrioplusLedbat::RoCEv2PrioplusLedbat(Ptr<RoCEv2SocketState> sockState)
    : RoCEv2CongestionOps(sockState, std::make_shared<Stats>()),
      m_stats(std::dynamic_pointer_cast<Stats>(RoCEv2CongestionOps::m_stats))
{
    NS_LOG_FUNCTION(this);
    Init();
}

RoCEv2PrioplusLedbat::~RoCEv2PrioplusLedbat()
{
    NS_LOG_FUNCTION(this);
}

void
RoCEv2PrioplusLedbat::SetReady()
{
    NS_LOG_FUNCTION(this);

    m_rngDelayError = CreateObject<UniformRandomVariable>();
    m_rngDelayError->SetAttribute("Min", DoubleValue(0));
    m_rngDelayError->SetAttribute("Max", DoubleValue(1));
    m_rngProbeTime = CreateObject<UniformRandomVariable>();
    m_rngProbeTime->SetAttribute("Min", DoubleValue(0));
    m_rngProbeTime->SetAttribute("Max", DoubleValue(1));

    // Call base class's SetRateRatio to set the start rate ratio
    // As the SetRateRatio of this class may has PRR to affect the cwnd
    RoCEv2CongestionOps::SetRateRatio(m_startRateRatio);

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
RoCEv2PrioplusLedbat::UpdateStateSend(Ptr<Packet> packet)
{
    NS_LOG_FUNCTION(this << packet);
    RoCEv2Header roceHeader;
    packet->PeekHeader(roceHeader);
    // Do not need to add a PrioplusHeader into packet, record the sendTs directly
    m_inflightPkts.push_back(std::make_tuple(roceHeader.GetPSN(),
                                             Simulator::Now().GetNanoSeconds(),
                                             m_sockState->GetRateRatioPercent()));
}

void
RoCEv2PrioplusLedbat::UpdateStateWithGenACK(Ptr<Packet> packet, Ptr<Packet> ack)
{
    NS_LOG_FUNCTION(this << packet << ack);
    // Add a PrioplusHeader into ack, which record the ts of recv the data packet.
    // Note that the ACK has RoCEv2Header and AETHeader. And we should add PrioplusHeader between
    // them.
    RoCEv2Header roceHeader;
    PrioplusHeader prioplusHeader;
    ack->RemoveHeader(roceHeader);
    ack->AddHeader(prioplusHeader);
    ack->AddHeader(roceHeader);
}

void
RoCEv2PrioplusLedbat::UpdateStateWithRcvACK(Ptr<Packet> ack,
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
            if (!m_secondRtt)
            {
                // the rtt after the first rtt, wait for delay
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

    //
    // if (m_incastAvoidanceRate != 1)
    // {
    //     m_incastAvoidanceRate = std::min(
    //         1.,
    //         m_incastAvoidanceRate *
    //             std::pow(m_forgetFactor,
    //                      (double)(Simulator::Now() - m_incastAvoidanceTime).GetNanoSeconds() /
    //                          m_sockState->GetBaseRtt().GetNanoSeconds()));
    // }

    // Calculate the delay from the ACK
    if (Simulator::GetContext() == 5 && ackSeq == 15988)
        NS_LOG_DEBUG(Simulator::Now().GetNanoSeconds());
    PrioplusHeader prioplusHeader;
    ack->RemoveHeader(prioplusHeader);
    auto [sendPsn, sendTs, _] = *(m_inflightPkts.begin());
    // try to absorb loss but not reordering
    while (sendPsn != ackSeq - 1)
    {
        // Pop the first element in m_inflightPkts after using it
        m_inflightPkts.pop_front();
        sendPsn = std::get<0>(*(m_inflightPkts.begin()));
        sendTs = std::get<1>(*(m_inflightPkts.begin()));
    }
    // Here we assume no packet loss or reordering in the network
    // NS_ASSERT_MSG(sendPsn == ackSeq - 1, "PSN not match when recv an ACK for PrioplusCC");
    Time delay;
    if (m_rttBased)
    {
        delay = Simulator::Now() - NanoSeconds(sendTs);
    }
    else
    {
        delay = prioplusHeader.GetTs() - NanoSeconds(sendTs);
    }

    // Pop the first element in m_inflightPkts after using it
    m_inflightPkts.pop_front();

    // double curRateRatio = m_sockState->GetRateRatioPercent();
    uint32_t curCwnd = m_sockState->GetCwnd();

    AddDelay(m_noiseFilter, delay, m_noiseFilterLen);

    // if (Simulator::GetContext() == 0 && Simulator::Now().GetNanoSeconds() > 100500000 &&
    //     Simulator::Now().GetNanoSeconds() < 100800000)
    //     NS_LOG_DEBUG(Simulator::Now().GetNanoSeconds() << " Delay: " << delay.GetNanoSeconds());

    if (delay > m_tHighThreshold && !m_weighted)
    {
        // Set curRateRatio to 0, which will be corrected to minRateRatio in CheckRateRatio
        // curRateRatio = 0;
        // curRateRatio = m_sockState->CheckRateRatio(curRateRatio);
        // m_nRttInDelayTooLow = 0;
        // std::cout << Simulator::Now().GetPicoSeconds() << " " << Simulator::GetContext()
        //           << " obeserve delay at " << delay.GetPicoSeconds() << "ps" << std::endl;
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

            // NS_LOG_DEBUG(Simulator::GetContext());
            m_incastAvoidanceRate = std::min(m_incastAvoidanceRate, 1.0 / concurrentFlowNum);
            // if (Simulator::GetContext() <= 3)
            //     std::cout << Simulator::Now().GetPicoSeconds() << " " << Simulator::GetContext()
            //               << " Incast detected, concurrent flow num: " << concurrentFlowNum
            //               << ", from delay: " << concurrentFlowNumFromDelay
            //               << ", m_incastAvoidanceRate " << m_incastAvoidanceRate << std::endl;
            m_incastAvoidanceTime = Simulator::Now();
        }

        // NS_LOG_DEBUG(Simulator::Now().GetNanoSeconds()
        //              << " Delay too high, stop sending and start probe");
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
            Stats::PrioplusDelayCompleteStats{Simulator::Now(),
                                             delay,
                                             (uint32_t)m_sockState->GetCwnd(),
                                             m_incastAvoidanceRate,
                                             0,
                                             0,
                                             0});
    }
    else
    {
        DoRateUpdate(0, delay, 0, rttPass && m_secondRtt, curCwnd);
    }

    if (curCwnd != 0 && m_probeEvent.IsRunning())
    {
        m_probeEvent.Cancel();
    }

    // Set the rate ratio to sockState
    // SetRateRatio(curRateRatio);
    SetCwnd(curCwnd);
}

void
RoCEv2PrioplusLedbat::UpdateStateWithRto()
{
    // If prioplus cc in probe state, send a probe packet now
    // Check probe state by cwnd
    if (m_sockState->GetCwnd() == 0)
    {
        ScheduleProbePacket(m_sockState->GetBaseOneWayDelay() + m_rttCorrection);
    }
}

void
RoCEv2PrioplusLedbat::InitCircBuf(OwdCircBuf& buffer)
{
    NS_LOG_FUNCTION(this);
    buffer.buffer.clear();
    buffer.min = 0;
}

Time
RoCEv2PrioplusLedbat::MinCircBuf(OwdCircBuf& b)
{
    NS_LOG_FUNCTION_NOARGS();
    if (b.buffer.empty())
    {
        return Time(UINT64_MAX);
    }
    else
    {
        return b.buffer[b.min];
    }
}

Time
RoCEv2PrioplusLedbat::CurrentDelay(FilterFunction filter)
{
    NS_LOG_FUNCTION(this);
    return filter(m_noiseFilter);
}

void
RoCEv2PrioplusLedbat::AddDelay(OwdCircBuf& cb, Time owd, uint32_t maxlen)
{
    NS_LOG_FUNCTION(this << owd << maxlen << cb.buffer.size());
    if (cb.buffer.empty())
    {
        NS_LOG_LOGIC("First Value for queue");
        cb.buffer.push_back(owd);
        cb.min = 0;
        return;
    }
    cb.buffer.push_back(owd);
    if (cb.buffer[cb.min] > owd)
    {
        cb.min = static_cast<uint32_t>(cb.buffer.size() - 1);
    }
    if (cb.buffer.size() >= maxlen)
    {
        NS_LOG_LOGIC("Queue full" << maxlen);
        cb.buffer.erase(cb.buffer.begin());
        cb.min = 0;
        NS_LOG_LOGIC("Current min element" << cb.buffer[cb.min]);
        for (uint32_t i = 1; i < maxlen - 1; i++)
        {
            if (cb.buffer[i] < cb.buffer[cb.min])
            {
                cb.min = i;
            }
        }
    }
}

void
RoCEv2PrioplusLedbat::DoRateUpdate(double refenceRate,
                                  Time delay,
                                  double scaleFactor,
                                  bool shouldAi,
                                  uint32_t& curCwnd)
{
    // Update curRateRatio to make current rate approach the line rate
    // Mimic the control in swift

    // if (Simulator::Now().GetPicoSeconds() == 100608159395)
    // {
    //     NS_LOG_DEBUG(Simulator::Now().GetPicoSeconds());
    // }

    Time targetDelay = m_tLowThreshold;

    if (delay < targetDelay)
    {
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
            // curCwnd += std::min(curCwnd, miPart);
            m_miBytes = std::min(curCwnd / 2, miPart);
        }

        // m_stats->RecordCcRateChange(true);
    }

    Time currentDelay = CurrentDelay(&RoCEv2PrioplusLedbat::MinCircBuf);
    // Time queueDelay = currentDelay - m_sockState->GetBaseOneWayDelay();
    // double offset = (m_target - queueDelay).GetSeconds() * m_gain / m_target.GetSeconds();
    // only can used in one way delay mode
    double offset = (m_tLowThreshold - delay).GetSeconds() * m_gain /
                    (m_tLowThreshold - m_sockState->GetBaseOneWayDelay()).GetSeconds();

    // Do AI
    uint64_t totalPacketSize = m_sockState->GetPacketSize();
    double cwnd = m_sockState->GetCwnd(); // Bytes
    double cwndPackets = (cwnd + totalPacketSize - 1.0) / totalPacketSize;
    if (cwndPackets < 1)
    {
        cwndPackets = 1.0;
    }
    double aiScaleFactor = m_incastAvoidanceRate > 0.2 ? 1 : m_incastAvoidanceRate * 5;
    // curCwnd = curCwnd + m_aiBytes / cwndPackets * aiScaleFactor + m_miBytes / cwndPackets;
    curCwnd += offset * totalPacketSize / cwndPackets * aiScaleFactor + m_miBytes / cwndPackets;
    cwnd = std::max(cwnd, static_cast<double>(totalPacketSize));
}

void
RoCEv2PrioplusLedbat::SetCwnd(uint32_t cwnd)
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
RoCEv2PrioplusLedbat::ResetRttRecord()
{
    m_lastUpdateSeq = m_sockState->GetTxBuffer()->GetFrontPsn();
}

void
RoCEv2PrioplusLedbat::StopSendingAndStartProbe(Time delay)
{
    // To stop sending, we set the cwnd to 0
    m_sockState->SetCwnd(0);

    m_stats->RecordCompleteStats(Stats::PrioplusDelayCompleteStats{Simulator::Now(),
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
RoCEv2PrioplusLedbat::SendProbePacket()
{
    // Check if a probe is just sent
    // if (Simulator::Now().GetNanoSeconds() > 100594499)
    // {
    //     NS_LOG_DEBUG(Simulator::Now().GetNanoSeconds());
    // }
    if (m_probeEvent.IsRunning())
    {
        return;
    }

    NS_ASSERT_MSG(!m_sendOutbandPktCb.IsNull(), "SendOutbandPktCb not set!");
    // Check if the flow is stopped
    if (CheckStopCondition())
        return;

    CongestionTypeTag ctTag(GetTypeId().GetUid());
    ProbePacketTag ppTag(true);
    std::vector<std::reference_wrapper<const Tag>> packetTags{ctTag, ppTag};

    // Send out-of-band probe packet
    bool success = m_sendOutbandPktCb(m_probeSeq, true, packetTags);
    if (success)
    {
        // m_inflightProbes.push_back(std::make_pair(m_probeSeq,
        // Simulator::Now().GetNanoSeconds()));
        m_inflightProbes[m_probeSeq] = Simulator::Now().GetNanoSeconds();
        m_probeSeq += 1;
        // Log the time and seq of the probe
        NS_LOG_DEBUG(Simulator::Now().GetNanoSeconds() << " Send probe " << m_probeSeq - 1);
    }
    else
    {
        NS_LOG_WARN("Send probe failed!");
    }

    // Schedule next probe
    // This event is just a placeholder and prevent for probe lost, the real probe event is
    // scheduled in UpdateStateWithOutbandPkt
    // m_probeEvent =
    //     Simulator::Schedule(100 * m_probeInterval, &RoCEv2PrioplusLedbat::SendProbePacket,
    //     this);
}

void
RoCEv2PrioplusLedbat::ScheduleProbePacket(Time delay)
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
        Simulator::Schedule(qDelay + randomDelay, &RoCEv2PrioplusLedbat::SendProbePacket, this);
    // std::cout << Simulator::Now().GetPicoSeconds() << " " << Simulator::GetContext()
    //   << " Schedule probe after " << (qDelay + randomDelay).GetPicoSeconds() << "ps"
    //   << std::endl;
    NS_LOG_DEBUG(Simulator::Now().GetPicoSeconds()
                 << " " << Simulator::GetContext() << " Schedule probe after "
                 << (qDelay + randomDelay).GetPicoSeconds() << "ps");
}

void
RoCEv2PrioplusLedbat::UpdateStateWithOutbandPkt(Ptr<Packet> probe,
                                               const RoCEv2Header& roce,
                                               uint32_t senderNextPSN)
{
    uint32_t ackSeq = roce.GetPSN();
    // Calculate the delay from the ACK
    PrioplusHeader prioplusHeader;
    probe->RemoveHeader(prioplusHeader);
    // auto [sendPsn, sendTs] = *(m_inflightProbes.begin());
    // Pop the first element in m_inflightPkts after using it
    // m_inflightProbes.pop_front();
    uint64_t sendTs = m_inflightProbes[ackSeq];

    // Here we assume no packet loss or reordering in the network
    // NS_ASSERT_MSG(sendPsn == ackSeq, "PSN not match when recv an probe ACK for PrioplusCC");
    Time delay;
    if (m_rttBased)
    {
        delay = Simulator::Now() - NanoSeconds(sendTs);
    }
    else
    {
        delay = prioplusHeader.GetTs() - NanoSeconds(sendTs);
    }
    // m_stats->RecordPacketDelay(NanoSeconds(sendTs), delay);

    if (Simulator::Now().GetNanoSeconds() > 104144771)
        NS_LOG_DEBUG(Simulator::Now().GetNanoSeconds()
                     << " " << Simulator::GetContext()
                     << " Probe Delay: " << delay.GetNanoSeconds());
    // std::cout << Simulator::Now().GetNanoSeconds() << " " << Simulator::GetContext()
    //           << " Probe Delay: " << delay.GetNanoSeconds() << "ns" << std::endl;

    if (delay > m_tHighThreshold && !m_weighted)
    {
        // Delay too high, schedule next probe after queueing delay + random delay
        ScheduleProbePacket(delay);

        // Reset the estimated concurrent flow number
        // m_incastAvoidanceRate = 1;
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
            Stats::PrioplusDelayCompleteStats{Simulator::Now(),
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
            Stats::PrioplusDelayCompleteStats{Simulator::Now(),
                                             delay,
                                             (uint32_t)m_sockState->GetCwnd(),
                                             m_incastAvoidanceRate,
                                             0,
                                             0,
                                             0});
    }
}

std::string
RoCEv2PrioplusLedbat::GetName() const
{
    return "Prioplus";
}

void
RoCEv2PrioplusLedbat::SetTlowInBytes(StringValue tlow)
{
    m_tLowThresholdInBytes = QueueSize(tlow.Get());
}

void
RoCEv2PrioplusLedbat::SetThighInBytes(StringValue thigh)
{
    m_tHighThresholdInBytes = QueueSize(thigh.Get());
}

Time
RoCEv2PrioplusLedbat::ConvertBytesToTime(QueueSize bytes)
{
    return Time::FromDouble(bytes.GetValue() * 8. / m_sockState->GetDeviceRate()->GetBitRate(),
                            Time::S);
}

void
RoCEv2PrioplusLedbat::Init()
{
    PrioplusHeader hd;
    m_extraAckSize += hd.GetSerializedSize(); // ACK has extra PrioplusHeader

    m_tLastDelay = Time(0);
    m_tLastPktSendTime = Time(0);
    m_lastUpdateSeq = 0;
    m_probeSeq = 0;

    m_secondRtt = false;
    m_miBytes = 0;

    RegisterCongestionType(GetTypeId());
}

std::shared_ptr<RoCEv2CongestionOps::Stats>
RoCEv2PrioplusLedbat::GetStats() const
{
    return m_stats;
}

RoCEv2PrioplusLedbat::Stats::Stats()
{
    NS_LOG_FUNCTION(this);
    BooleanValue bv;
    if (GlobalValue::GetValueByNameFailSafe("detailedSenderStats", bv))
        bDetailedSenderStats = bv.Get();
    else
        bDetailedSenderStats = false;
}

void
RoCEv2PrioplusLedbat::Stats::RecordCompleteStats(
    RoCEv2PrioplusLedbat::Stats::PrioplusDelayCompleteStats&& stats)
{
    if (bDetailedSenderStats)
    {
        vPrioplusCompleteStats.push_back(stats);
    }
}

} // namespace ns3
