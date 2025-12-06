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
#include "rocev2-credit-spraying.h"

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
                            .SetGroupName("Dcb");
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
    SendCreditRequest(m_sockState->GetBaseOneWayDelay());
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

    NS_ASSERT_MSG(!m_sendOutbandPktCb.IsNull(), "SendOutbandPktCb not set!");
    // Check if the flow is stopped
    if (CheckStopCondition())
        return;

    CongestionTypeTag ctTag(GetTypeId().GetUid());
    ProbePacketTag ppTag(true);
    std::vector<std::reference_wrapper<const Tag>> packetTags{ctTag, ppTag};

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
    // Cancel previous probe event
    if (m_cReqTimeOut.IsRunning())
        m_cReqTimeOut.Cancel();
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
RoCEv2CreditSpraying::UpdateStateWithRcvACK(Ptr<Packet> ack,
                                            const RoCEv2Header& roce,
                                            const uint32_t senderNextPSN)
{
    // credit spraying 收到 ACK&Credit 的逻辑
    // 计算 ACK 的包的个数，SetCwnd更新窗口
    NS_LOG_FUNCTION(this << ack << roce);
    uint32_t ackedPkts = std::max((uint32_t)0, roce.GetPSN() - m_sockState->GetTxBuffer()->GetFrontPsn());
    m_sockState->SetCwnd(m_sockState->GetCwnd() + 1 - ackedPkts);
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

RoCEv2CreditSpraying::Stats::Stats()
{
    NS_LOG_FUNCTION(this);
    BooleanValue bv;
    if (GlobalValue::GetValueByNameFailSafe("detailedSenderStats", bv))
        bDetailedSenderStats = bv.Get();
    else
        bDetailedSenderStats = false;
}

} // namespace ns3
