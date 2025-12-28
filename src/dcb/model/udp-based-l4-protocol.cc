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
 * Author: Pavinberg <pavin0702@gmail.com>
 */

#include "udp-based-l4-protocol.h"

#include "rocev2-l4-protocol.h"
#include "rocev2-socket.h"
#include "udp-based-socket.h"

#include "ns3/fatal-error.h"
#include "ns3/ip-l4-protocol.h"
#include "ns3/ipv4-address.h"
#include "ns3/ipv4-end-point-demux.h"
#include "ns3/ipv4.h"
#include "ns3/log-macros-enabled.h"
#include "ns3/node.h"
#include "ns3/packet.h"
#include "ns3/simulator.h"
#include "ns3/udp-header.h"
#include "ns3/udp-l4-protocol.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("UdpBasedL4Protocol");

NS_OBJECT_ENSURE_REGISTERED(UdpBasedL4Protocol);

TypeId
UdpBasedL4Protocol::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::UdpBasedL4Protocol").SetParent<IpL4Protocol>().SetGroupName("Dcb");
    return tid;
}

UdpBasedL4Protocol::UdpBasedL4Protocol()
    : m_node(nullptr),
      m_udp(nullptr),
      m_udpEndPoint(nullptr)
{
    NS_LOG_FUNCTION(this);
    m_innerEndPoints = new InnerEndPointDemux();
}

UdpBasedL4Protocol::~UdpBasedL4Protocol()
{
    NS_LOG_FUNCTION(this);
}

Ptr<Socket>
UdpBasedL4Protocol::CreateSocket()
{
    NS_LOG_FUNCTION(this);
    Ptr<UdpBasedSocket> socket = CreateObject<UdpBasedSocket>();
    socket->SetNode(m_node);
    socket->SetInnerUdpProtocol(this);
    m_sockets.emplace_back(socket);
    return socket;
}

uint16_t
UdpBasedL4Protocol::GetProtocolNumber(void) const
{
    return PROT_NUMBER;
}

void
UdpBasedL4Protocol::NotifyNewAggregate()
{
    // NS_FATAL_ERROR("Not here");
    // Do nothing as the configuration is done otherwhere
    NS_LOG_FUNCTION(this);

    /* Previous code, just keep it here for reference
    Ptr<Node> node = this->GetObject<Node>();
    Ptr<UdpL4Protocol> udp = this->GetObject<UdpL4Protocol>();
    if (udp == 0)
    {
        NS_FATAL_ERROR("UdpBasedL4Protocol must be aggregated after aggregating UdpL4Protocol");
    }
    if (m_node == 0 && node && node->GetObject<UdpBasedSocketFactory>() == 0)
    {
        // Aggregate UdpBasedSocketFacoty to the node
        m_node = node;
        m_udp = udp;
        Ptr<UdpBasedSocketFactory> socketFactory = CreateObject<UdpBasedSocketFactory>();
        node->AggregateObject(socketFactory);
    }
    */

    Object::NotifyNewAggregate();
}

void
UdpBasedL4Protocol::Setup(Ptr<Node> node, Ptr<NetDevice> dev)
{
    NS_LOG_FUNCTION(this << node << dev);

    m_node = node;
    m_udp = node->GetObject<UdpL4Protocol>();
    NS_ASSERT_MSG(m_udp != nullptr,
                  "UdpL4Protocol must be aggregated before UdpBasedL4Protocol::Setup()");
    m_udpEndPoint = m_udp->Allocate(dev, GetProtocolNumber());
    // m_udpEndPoint->BindToNetDevice(dev);
    FinishSetup(m_udpEndPoint); // bind callbacks
}

void
UdpBasedL4Protocol::ForwardUp(Ptr<Packet> packet,
                              Ipv4Header header,
                              uint16_t port,
                              Ptr<Ipv4Interface> incomingIntf)
{
    NS_LOG_FUNCTION(this << packet << header << port << incomingIntf);

    InnerPortInfo portInfo = ParseInnerPorts(packet, header, port, incomingIntf);
    // Prefer per-flow endpoint (dstQP, srcIP, srcQP) if a dedicated socket was created.
    InnerEndPoint* endPoint = m_innerEndPoints->Lookup(portInfo.dstPort, portInfo.srcAddr, portInfo.srcPort);
    if (endPoint == nullptr)
    {
        // Fallback to dstQP-only listener endpoint.
        endPoint = m_innerEndPoints->Lookup(portInfo.dstPort);
    }
    if (endPoint)
    {
        endPoint->ForwardUp(packet, header, portInfo.dstPort, incomingIntf);
    }
    else
    {
        // Check if the packet is a RoCEv2 CNP packet
        if (RoCEv2L4Protocol::IsCNP(packet))
        {
            NS_LOG_DEBUG("Received a CNP packet for a closed socket.");
            return;
        }
        // Check if the packet is a probe packet
        ProbePacketTag ppt;
        if (packet->PeekPacketTag(ppt))
        {
            NS_LOG_DEBUG("Received a probe packet for a closed socket.");
            return;
        }
        // NS_LOG_WARN("No endPoints matched in UDP-based L4 protocol with inner port "
        //             << portInfo.dstPort << " on node " << Simulator::GetContext()
        //             << " on time " << Simulator::Now());
    }
}

void
UdpBasedL4Protocol::Destroy()
{
}

InnerEndPoint*
UdpBasedL4Protocol::Allocate()
{
    return m_innerEndPoints->Allocate(GetDefaultServicePort());
}

Ipv4Address
UdpBasedL4Protocol::GetLocalAddress() const
{
    NS_LOG_FUNCTION(this);
    return m_udpEndPoint->GetLocalAddress();
}

void
UdpBasedL4Protocol::Send(Ptr<Packet> packet,
                         Ipv4Address saddr,
                         Ipv4Address daddr,
                         uint32_t sport,
                         uint32_t dport,
                         Ptr<Ipv4Route> route)
{
    NS_LOG_FUNCTION(this << packet << saddr << daddr << sport << dport << route);
    m_udp->Send(packet, saddr, daddr, GetProtocolNumber(), GetProtocolNumber(), route);
}

void
UdpBasedL4Protocol::DeAllocate(InnerEndPoint* endPoint)
{
    m_innerEndPoints->DeAllocate(endPoint);
}

void
UdpBasedL4Protocol::DoDispose()
{
    NS_LOG_FUNCTION(this);
    m_node = 0;
    m_udp = 0;
    m_sockets.clear();
    delete m_innerEndPoints;
}

InnerEndPoint::InnerEndPoint(uint32_t localPort, uint32_t destPort)
    : m_localPort(localPort),
      m_peerPort(destPort),
      m_rxEnabled(true)
{
}

InnerEndPoint::~InnerEndPoint()
{
}

void
InnerEndPoint::ForwardUp(Ptr<Packet> p,
                         const Ipv4Header& header,
                         uint32_t sport,
                         Ptr<Ipv4Interface> incomingIntf)
{
    if (!m_rxCallback.IsNull())
    {
        m_rxCallback(p, header, sport, incomingIntf);
    }
}

uint32_t
InnerEndPoint::GetLocalPort() const
{
    return m_localPort;
}

uint32_t
InnerEndPoint::GetPeerPort() const
{
    return m_peerPort;
}

void
InnerEndPoint::SetPeerPort(uint32_t port)
{
    m_peerPort = port;
}

void
InnerEndPoint::SetRxCallback(
    Callback<void, Ptr<Packet>, Ipv4Header, uint32_t, Ptr<Ipv4Interface>> callback)
{
    m_rxCallback = callback;
}

void
InnerEndPoint::SetRxEnabled(bool enabled)
{
    m_rxEnabled = enabled;
}

bool
InnerEndPoint::IsRxEnabled() const
{
    return m_rxEnabled;
}

InnerEndPointDemux::InnerEndPointDemux()
    : m_innerPortFirst(1),
      m_innerPortLast(0xffff),
      m_ephemeral(1)
{
}

InnerEndPointDemux::InnerEndPointDemux(uint32_t portFirst, uint32_t portLast)
    : m_innerPortFirst(portFirst),
      m_innerPortLast(portLast),
      m_ephemeral(portFirst)
{
}

InnerEndPointDemux::~InnerEndPointDemux()
{
    for (const auto& p : m_endPoints)
    {
        delete p.second;
    }
    m_endPoints.clear();
    for (const auto& p : m_flowEndPoints)
    {
        delete p.second;
    }
    m_flowEndPoints.clear();
}

InnerEndPoint*
InnerEndPointDemux::Allocate(uint32_t dport)
{
    uint32_t sport = AllocateEphemeralPort();
    if (sport == 0)
    {
        NS_FATAL_ERROR("Ephemeral port allocation failed.");
    }
    return Allocate(sport, dport);
}

InnerEndPoint*
InnerEndPointDemux::Allocate(uint32_t sport, uint32_t dport)
{
    if (m_endPoints.find(sport) != m_endPoints.end())
    {
        NS_FATAL_ERROR("Inner port " << sport << " has already been allocated.");
    }
    InnerEndPoint* endPoint = new InnerEndPoint(sport, dport);
    m_endPoints.emplace(sport, endPoint);
    NS_LOG_DEBUG("Now have " << m_endPoints.size() << " endpoints.");
    return endPoint;
}

void
InnerEndPointDemux::DeAllocate(InnerEndPoint* endPoint)
{
    m_endPoints.erase(endPoint->GetLocalPort());
    delete endPoint;
}

InnerEndPoint*
InnerEndPointDemux::Lookup(uint32_t innerPort)
{
    std::map<uint32_t, InnerEndPoint*>::const_iterator p = m_endPoints.find(innerPort);
    if (p != m_endPoints.end())
    {
        InnerEndPoint* endPoint = p->second;
        if (endPoint->IsRxEnabled())
        {
            return endPoint;
        }
    }
    return nullptr;
}

InnerEndPoint*
InnerEndPointDemux::Lookup(uint32_t innerPort, Ipv4Address srcAddr, uint32_t srcPort)
{
    FlowKey key{innerPort, srcAddr, srcPort};
    auto it = m_flowEndPoints.find(key);
    if (it != m_flowEndPoints.end())
    {
        InnerEndPoint* endPoint = it->second;
        if (endPoint->IsRxEnabled())
        {
            return endPoint;
        }
    }
    return nullptr;
}

bool
InnerEndPointDemux::LookupPortLocal(uint32_t port)
{
    std::map<uint32_t, InnerEndPoint*>::const_iterator p = m_endPoints.find(port);
    return p != m_endPoints.end();
}

uint32_t
InnerEndPointDemux::AllocateEphemeralPort()
{
    uint32_t port = m_ephemeral;
    // Max number of ports to try before giving up
    int64_t count = static_cast<int64_t>(m_innerPortLast - m_innerPortFirst);
    do
    {
        if (count-- < 0)
        {
            return 0;
        }
        port++;
        if (port > m_innerPortLast)
        {
            /*TODO
             * SrcQp wrap problem has not been solved.
             * We have found these problems:
             * 1. Stats uses flowId to identify flows, but flowId is not unique.
             * 2. Receiver uses flowId to identify the sender, but flowId is not unique.
             * Therefore, for now, we just log the error.
             */
            NS_FATAL_ERROR("SrcQp wrap around. First is " << m_innerPortFirst << ", Last is "
                                                          << m_innerPortLast);
        }
        // Wrap around if we reach the end of the range
        if (port < m_innerPortFirst || port > m_innerPortLast)
        {
            port = m_innerPortFirst;
        }
    } while (LookupPortLocal(port));
    m_ephemeral = port;
    return port;
}

InnerEndPoint*
InnerEndPointDemux::AllocateForFlow(uint32_t dstPort, Ipv4Address srcAddr, uint32_t srcPort)
{
    FlowKey key{dstPort, srcAddr, srcPort};
    if (m_flowEndPoints.find(key) != m_flowEndPoints.end())
    {
        return m_flowEndPoints[key];
    }
    // Per-flow endpoint is keyed by dstQP + srcIP + srcQP so that packets of a single flow are
    // steered to its dedicated socket instead of the listener.
    InnerEndPoint* endPoint = new InnerEndPoint(dstPort, srcPort);
    m_flowEndPoints.emplace(key, endPoint);
    return endPoint;
}

void
InnerEndPointDemux::DeAllocateFlow(uint32_t dstPort, Ipv4Address srcAddr, uint32_t srcPort)
{
    FlowKey key{dstPort, srcAddr, srcPort};
    auto it = m_flowEndPoints.find(key);
    if (it != m_flowEndPoints.end())
    {
        delete it->second;
        m_flowEndPoints.erase(it);
    }
}

} // namespace ns3
