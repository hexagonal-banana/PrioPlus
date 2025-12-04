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
 * Author: F.Y. Xue <xue.fyang@foxmail.com>
 */

#include "rocev2-nocc.h"

#include "rocev2-socket.h"

#include "ns3/global-value.h"
#include "ns3/simulator.h"

namespace ns3
{
NS_LOG_COMPONENT_DEFINE("RoCEv2Nocc");

NS_OBJECT_ENSURE_REGISTERED(RoCEv2Nocc);

TypeId
RoCEv2Nocc::GetTypeId(void)
{
    static TypeId tid = TypeId("ns3::RoCEv2Nocc")
                            .SetParent<RoCEv2CongestionOps>()
                            .AddConstructor<RoCEv2Nocc>()
                            .SetGroupName("Dcb")
                            .AddAttribute("FairShare",
                                          "Set the rate to the fair share",
                                          BooleanValue(false),
                                          MakeBooleanAccessor(&RoCEv2Nocc::m_fairShare),
                                          MakeBooleanChecker())
                            .AddAttribute("TargetInflight",
                                          "Set the target inflight",
                                          DoubleValue(1.0),
                                          MakeDoubleAccessor(&RoCEv2Nocc::m_targetInflight),
                                          MakeDoubleChecker<double>());
    return tid;
}

RoCEv2Nocc::RoCEv2Nocc()
    : RoCEv2CongestionOps(std::make_shared<Stats>()),
      m_stats(std::dynamic_pointer_cast<Stats>(RoCEv2CongestionOps::m_stats))
{
    Init();
}

RoCEv2Nocc::RoCEv2Nocc(Ptr<RoCEv2SocketState> sockState)
    : RoCEv2CongestionOps(sockState, std::make_shared<Stats>()),
      m_stats(std::dynamic_pointer_cast<Stats>(RoCEv2CongestionOps::m_stats))
{
    Init();
}

std::string
RoCEv2Nocc::GetName() const
{
    return "NoCC";
}

void
RoCEv2Nocc::Init()
{
    m_fairShare = false;
    RegisterCongestionType(GetTypeId());
}

void
RoCEv2Nocc::SetReady()
{
    if (!m_fairShare)
    {
        SetRateRatio(m_startRateRatio);
    }
    else
    {
        // Set the rate to fair share
        // Get the number of hosts
        UintegerValue nHostsValue;
        GlobalValue::GetValueByName("NHosts", nHostsValue);
        uint32_t nHosts = nHostsValue.Get();

        // The fair share is 1 / (nHost - 1) for each host
        SetRateRatio(1.0 / (nHosts - 1));

        if (m_targetInflight != 1.0)
        {
            // Set the target inflight
            m_sockState->SetCwnd(m_targetInflight * m_sockState->GetBaseBdp() / (nHosts - 1));
        }
    }
}

} // namespace ns3