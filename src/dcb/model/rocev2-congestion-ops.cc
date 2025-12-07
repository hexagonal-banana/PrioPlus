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
#include "rocev2-congestion-ops.h"

#include "rocev2-socket.h"

#include "ns3/global-value.h"
#include "ns3/rocev2-header.h"
#include "ns3/simulator.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("RoCEv2CongestionOps");
NS_OBJECT_ENSURE_REGISTERED(RoCEv2CongestionOps);

// Initialize static member
std::map<uint16_t, TypeId> RoCEv2CongestionOps::m_mCongestionTypeIds;

TypeId
RoCEv2CongestionOps::GetTypeId(void)
{
    static TypeId tid =
        TypeId("ns3::RoCEv2CongestionOps")
            .SetParent<Object>()
            .SetGroupName("Dcb")
            .AddAttribute("StartRateRatio",
                          "The start rate ratio of the flow",
                          DoubleValue(1.0),
                          MakeDoubleAccessor(&RoCEv2CongestionOps::m_startRateRatio),
                          MakeDoubleChecker<double>())
            .AddAttribute("MaxRateRatio",
                          "The max rate ratio of the flow",
                          DoubleValue(1.0),
                          MakeDoubleAccessor(&RoCEv2CongestionOps::m_maxRateRatio),
                          MakeDoubleChecker<double>())
            .AddAttribute("IsPacing",
                          "Whether the flow is pacing",
                          BooleanValue(false),
                          MakeBooleanAccessor(&RoCEv2CongestionOps::m_isPacing),
                          MakeBooleanChecker())
            .AddAttribute("IsLimiting",
                          "Whether the flow is limiting",
                          BooleanValue(false),
                          MakeBooleanAccessor(&RoCEv2CongestionOps::m_isLimiting),
                          MakeBooleanChecker())
            .AddAttribute("IsStaticLimiting",
                          "If static limiting, the cwnd is set to base BDP",
                          BooleanValue(false),
                          MakeBooleanAccessor(&RoCEv2CongestionOps::m_isStaticLimiting),
                          MakeBooleanChecker());
    return tid;
}

RoCEv2CongestionOps::RoCEv2CongestionOps(std::shared_ptr<Stats> stats)
    : m_stats(stats),
      m_extraHeaderSize(0),
      m_extraAckSize(0),
      m_isPacing(false),
      m_isLimiting(false)

{
    NS_LOG_FUNCTION(this);
}

RoCEv2CongestionOps::RoCEv2CongestionOps(Ptr<RoCEv2SocketState> sockState,
                                         std::shared_ptr<Stats> stats)
    : m_stats(stats),
      m_sockState(sockState),
      m_extraHeaderSize(0),
      m_extraAckSize(0),
      m_isPacing(false),
      m_isLimiting(false)

{
    NS_LOG_FUNCTION(this);
}

RoCEv2CongestionOps::~RoCEv2CongestionOps()
{
    NS_LOG_FUNCTION(this);
}

void
RoCEv2CongestionOps::SetStopTime(Time stopTime)
{
    m_stopTime = stopTime;
}

void
RoCEv2CongestionOps::SetSockState(Ptr<RoCEv2SocketState> sockState)
{
    m_sockState = sockState;
}

/**
 * \brief Check whether the sending is stopped (typically by all data has been acked).
 */
bool
RoCEv2CongestionOps::CheckStopCondition()
{
    return Simulator::Now() >= m_stopTime;
}

void
RoCEv2CongestionOps::SetRateRatio(double rateRatio)
{
    rateRatio = std::min(rateRatio, m_maxRateRatio);
    m_sockState->SetRateRatioPercent(rateRatio);
    // if limiting, set cwnd
    if (m_isLimiting)
    {
        uint64_t baseBdp = m_sockState->GetBaseBdp();
        if (baseBdp == 0)
        {
            return;
        }
        if (m_isStaticLimiting)
        {
            NS_LOG_DEBUG("Set CWND to baseBdp");
            m_sockState->SetCwnd(baseBdp);
        }
        else
        {
            double curRateRatio = m_sockState->GetRateRatioPercent();
            // Calculate CWND based on rateRatio and baseBdp
            uint64_t cwnd = static_cast<uint64_t>(curRateRatio * baseBdp);
            m_sockState->SetCwnd(cwnd);
        }
    }
    m_stats->RecordCcRate(*(m_sockState->GetDeviceRate()) * m_sockState->GetRateRatioPercent());
}

void
RoCEv2CongestionOps::SetCwnd(uint64_t cwnd)
{
    cwnd = std::max(cwnd, uint64_t(m_sockState->GetMinRateRatio() * m_sockState->GetBaseBdp()));
    uint64_t baseBdp = m_sockState->GetBaseBdp();
    // if pacing, set rateRatio
    if (m_isPacing)
    {
        if (baseBdp == 0)
        {
            return;
        }
        else if (cwnd > baseBdp)
        {
            NS_LOG_DEBUG("CWND is larger than baseBdp, set CWND to baseBdp");
            cwnd = baseBdp;
        }
        double curRateRatio = static_cast<double>(cwnd) / baseBdp;
        m_sockState->SetRateRatioPercent(curRateRatio);
        m_stats->RecordCcRate(*(m_sockState->GetDeviceRate()) * m_sockState->GetRateRatioPercent());
    }
    
    if (cwnd / m_sockState->GetPacketSize() < 1)
    {
        // pacing
        double curRateRatio = static_cast<double>(cwnd) / baseBdp;
        m_sockState->SetRateRatioPercent(curRateRatio);
    }
    else
    {
        // recover the rate to line rate
        m_sockState->SetRateRatioPercent(m_maxRateRatio);
    }

    m_sockState->SetCwnd(cwnd);
}

std::shared_ptr<RoCEv2CongestionOps::Stats>
RoCEv2CongestionOps::GetStats() const
{
    return m_stats;
}

void
RoCEv2CongestionOps::SetSendOutbandPktCb(SendOutbandPktCb cb)
{
    m_sendOutbandPktCb = cb;
}

void
RoCEv2CongestionOps::SetSendPendingDataCb(SendPendingDataCb cb)
{
    m_sendPendingDataCb = cb;
}

RoCEv2CongestionOps::Stats::Stats()
{
    NS_LOG_FUNCTION(this);
    BooleanValue bv;
    if (GlobalValue::GetValueByNameFailSafe("detailedSenderStats", bv))
        bDetailedSenderStats = bv.Get();
    else
        bDetailedSenderStats = false;
}

void
RoCEv2CongestionOps::Stats::RecordCcRate(DataRate rate)
{
    if (bDetailedSenderStats)
    {
        if (vCcRate.size() > 0 && vCcRate.back().second == rate)
        {
            return;
        }
        vCcRate.push_back(std::make_pair(Simulator::Now(), rate));
    }
}

NS_OBJECT_ENSURE_REGISTERED(CongestionTypeTag);

TypeId
CongestionTypeTag::GetTypeId()
{
    static TypeId tid = TypeId("ns3::CongestionTypeTag")
                            .SetParent<Tag>()
                            .SetGroupName("Network")
                            .AddConstructor<CongestionTypeTag>();
    return tid;
}

TypeId
CongestionTypeTag::GetInstanceTypeId() const
{
    return GetTypeId();
}

uint32_t
CongestionTypeTag::GetSerializedSize() const
{
    NS_LOG_FUNCTION(this);
    return 2;
}

void
CongestionTypeTag::Serialize(TagBuffer buf) const
{
    NS_LOG_FUNCTION(this << &buf);
    buf.WriteU16(m_congestionType);
}

void
CongestionTypeTag::Deserialize(TagBuffer buf)
{
    NS_LOG_FUNCTION(this << &buf);
    m_congestionType = buf.ReadU16();
}

void
CongestionTypeTag::Print(std::ostream& os) const
{
    NS_LOG_FUNCTION(this << &os);
    os << "FlowId=" << m_congestionType;
}

CongestionTypeTag::CongestionTypeTag()
    : Tag()
{
    NS_LOG_FUNCTION(this);
}

CongestionTypeTag::CongestionTypeTag(uint16_t congestionType)
    : Tag(),
      m_congestionType(congestionType)
{
    NS_LOG_FUNCTION(this << congestionType);
}

void
CongestionTypeTag::SetCongestionTypeIdx(uint16_t congestionType)
{
    NS_LOG_FUNCTION(this << congestionType);
    m_congestionType = congestionType;
}

uint16_t
CongestionTypeTag::GetCongestionTypeIdx() const
{
    NS_LOG_FUNCTION(this);
    return m_congestionType;
}

TypeId
CongestionTypeTag::GetCongestionTypeId() const
{
    NS_LOG_FUNCTION(this);
    return RoCEv2CongestionOps::GetCongestionTypeId(m_congestionType);
}

} // namespace ns3
