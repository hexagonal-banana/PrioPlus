/*
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
 * Author: Pavinberg <pavin0702@gmail.com>
 */

#include "dcb-channel.h"

#include "ns3/simulator.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("DcbChannel");

NS_OBJECT_ENSURE_REGISTERED(DcbChannel);

TypeId
DcbChannel::GetTypeId(void)
{
    static TypeId tid = TypeId("ns3::DcbChannel")
                            .SetParent<Channel>()
                            .SetGroupName("Dcb")
                            .AddConstructor<DcbChannel>()
                            .AddAttribute("Delay",
                                          "Propagation delay through the channel",
                                          TimeValue(Seconds(0)),
                                          MakeTimeAccessor(&DcbChannel::m_delay),
                                          MakeTimeChecker())
                            .AddTraceSource("TxRxDcb",
                                            "Trace source indicating transmission of packet "
                                            "from the DcbChannel, used by the Animation "
                                            "interface.",
                                            MakeTraceSourceAccessor(&DcbChannel::m_txrxDcb),
                                            "ns3::DcbChannel::TxRxAnimationCallback");
    return tid;
}

//
// By default, you get a channel that
// has an "infitely" fast transmission speed and zero delay.
DcbChannel::DcbChannel()
    : Channel(),
      m_delay(Seconds(0.)),
      m_nDevices(0)
{
    NS_LOG_FUNCTION_NOARGS();
}

void
DcbChannel::Attach(Ptr<DcbNetDevice> device)
{
    NS_LOG_FUNCTION(this << device);
    NS_ASSERT_MSG(m_nDevices < N_DEVICES, "Only two devices permitted");
    NS_ASSERT(device != nullptr);

    m_link[m_nDevices++].m_src = device;
    //
    // If we have both devices connected to the channel, then finish introducing
    // the two halves and set the links to IDLE.
    //
    if (m_nDevices == N_DEVICES)
    {
        m_link[0].m_dst = m_link[1].m_src;
        m_link[1].m_dst = m_link[0].m_src;
        m_link[0].m_state = IDLE;
        m_link[1].m_state = IDLE;
    }
}

bool
DcbChannel::TransmitStart(Ptr<const Packet> p, Ptr<DcbNetDevice> src, Time txTime)
{
    NS_LOG_FUNCTION(this << p << src);
    NS_LOG_LOGIC("UID is " << p->GetUid() << ")");

    NS_ASSERT(m_link[0].m_state != INITIALIZING);
    NS_ASSERT(m_link[1].m_state != INITIALIZING);

    uint32_t wire = src == m_link[0].m_src ? 0 : 1;

    Simulator::ScheduleWithContext(m_link[wire].m_dst->GetNode()->GetId(),
                                   txTime + m_delay,
                                   &DcbNetDevice::Receive,
                                   m_link[wire].m_dst,
                                   p->Copy());

    // Call the tx anim callback on the net device
    m_txrxDcb(p, src, m_link[wire].m_dst, txTime, txTime + m_delay);
    return true;
}

std::size_t
DcbChannel::GetNDevices(void) const
{
    NS_LOG_FUNCTION_NOARGS();
    return m_nDevices;
}

Ptr<DcbNetDevice>
DcbChannel::GetDcbDevice(std::size_t i) const
{
    NS_LOG_FUNCTION_NOARGS();
    NS_ASSERT(i < 2);
    return m_link[i].m_src;
}

Ptr<NetDevice>
DcbChannel::GetDevice(std::size_t i) const
{
    NS_LOG_FUNCTION_NOARGS();
    return GetDcbDevice(i);
}

Time
DcbChannel::GetDelay(void) const
{
    return m_delay;
}

void
DcbChannel::SetDelay(Time delay)
{
    m_delay = delay;
}

Ptr<DcbNetDevice>
DcbChannel::GetSource(uint32_t i) const
{
    return m_link[i].m_src;
}

Ptr<DcbNetDevice>
DcbChannel::GetDestination(uint32_t i) const
{
    return m_link[i].m_dst;
}

bool
DcbChannel::IsInitialized(void) const
{
    NS_ASSERT(m_link[0].m_state != INITIALIZING);
    NS_ASSERT(m_link[1].m_state != INITIALIZING);
    return true;
}

} // namespace ns3
