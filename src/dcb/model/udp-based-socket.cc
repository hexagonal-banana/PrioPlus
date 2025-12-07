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

#include "udp-based-socket.h"

#include "dcb-net-device.h"
#include "pausable-queue-disc.h"
#include "rocev2-l4-protocol.h"
#include "udp-based-l4-protocol.h"

#include "ns3/ethernet-header.h"
#include "ns3/inet-socket-address.h"
#include "ns3/ipv4-packet-info-tag.h"
#include "ns3/ipv4-routing-protocol.h"
#include "ns3/ipv4.h"
#include "ns3/log.h"
#include "ns3/node.h"
#include "ns3/object-base.h"
#include "ns3/object-factory.h"
#include "ns3/simulator.h"
#include "ns3/udp-header.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("UdpBasedSocket");

NS_OBJECT_ENSURE_REGISTERED(UdpBasedSocket);

// The correct maximum UDP message size is 65507, as determined by the following formula:
// 0xffff - (sizeof(IP Header) + sizeof(UDP Header)) = 65535-(20+8) = 65507
// \todo MAX_IPV4_UDP_DATAGRAM_SIZE is correct only for IPv4
static const uint32_t MAX_IPV4_UDP_DATAGRAM_SIZE = 65507; //!< Maximum UDP datagram size

TypeId
UdpBasedSocket::GetTypeId()
{
    static TypeId tid = TypeId("ns3::UdpBasedSocket")
                            .SetParent<Socket>()
                            .SetGroupName("Dcb")
                            .AddConstructor<UdpBasedSocket>()
                            .AddTraceSource("Drop",
                                            "Drop UDP packet due to receive buffer overflow",
                                            MakeTraceSourceAccessor(&UdpBasedSocket::m_dropTrace),
                                            "ns3::Packet::TracedCallback");
    return tid;
}

UdpBasedSocket::UdpBasedSocket()
    : m_node(0),
      m_innerProto(0),
      m_endPoint(0),
      m_errno(),
      m_shutdownSend(false),
      m_shutdownRecv(false),
      m_connected(false),
      m_rxUsed(0),
      m_rcvBufSize(8192) // TODO: do not use magic number
{
    NS_LOG_FUNCTION(this);
}

UdpBasedSocket::~UdpBasedSocket()
{
    NS_LOG_FUNCTION(this);
    m_node = 0;
}

void
UdpBasedSocket::SetInnerUdpProtocol(Ptr<UdpBasedL4Protocol> udp)
{
    NS_LOG_FUNCTION(this << udp);
    m_innerProto = udp;
}

enum Socket::SocketErrno
UdpBasedSocket::GetErrno(void) const
{
    NS_LOG_FUNCTION(this);
    return m_errno;
}

enum Socket::SocketType
UdpBasedSocket::GetSocketType(void) const
{
    return SocketType::NS3_SOCK_RAW;
}

void
UdpBasedSocket::SetNode(Ptr<Node> node)
{
    NS_LOG_FUNCTION(this << node);
    m_node = node;
}

Ptr<Node>
UdpBasedSocket::GetNode(void) const
{
    NS_LOG_FUNCTION(this);
    return m_node;
}

const uint32_t
UdpBasedSocket::GetSerializedHeaderSize() const
{
    NS_LOG_FUNCTION(this);
    return 14 + 20 + 8 +
           m_innerProto->GetInnerProtocolHeaderSize(); // Ethernet + IP + UDP + Inner header
}

int
UdpBasedSocket::Bind(const Address& address)
{
    NS_FATAL_ERROR("Should not call this Bind (Address) on UdpBasedSocket");
    return -1;
}

int
UdpBasedSocket::Bind()
{
    NS_LOG_FUNCTION(this);
    NS_ASSERT_MSG(m_boundnetdevice,
                  "UdpBasedSocket should be bound to a net device before calling Bind");
    // m_endPoint->BindToNetDevice (m_boundnetdevice);
    m_endPoint = m_innerProto->Allocate();

    m_endPoint->SetRxCallback(MakeCallback(&UdpBasedSocket::ForwardUp, Ptr<UdpBasedSocket>(this)));
    // m_endPoint->SetDestroyCallback (
    // MakeCallback (&UdpBasedSocket::Destroy, Ptr<UdpBasedSocket> (this)));
    return 0;
}

int
UdpBasedSocket::Bind6()
{
    NS_LOG_FUNCTION(this);
    NS_ASSERT_MSG(false, "Not implemented");
    return 0;
}

int
UdpBasedSocket::Close(void)
{
    NS_LOG_FUNCTION(this);

    if (m_shutdownRecv == true && m_shutdownSend == true)
    {
        m_errno = Socket::ERROR_BADF;
        return -1;
    }
    m_shutdownRecv = true;
    m_shutdownSend = true;

    // TODO: maybe more logic here
    // m_endPoint->SetDestroyCallback (MakeNullCallback<void> ());
    m_innerProto->DeAllocate(m_endPoint);
    m_endPoint = 0;
    return 0;
}

int
UdpBasedSocket::ShutdownSend(void)
{
    NS_LOG_FUNCTION(this);
    m_shutdownSend = true;
    return 0;
}

int
UdpBasedSocket::ShutdownRecv(void)
{
    NS_LOG_FUNCTION(this);
    m_shutdownRecv = true;
    if (m_endPoint)
    {
        m_endPoint->SetRxEnabled(false);
    }
    return 0;
}

int
UdpBasedSocket::Connect(const Address& address)
{
    NS_LOG_FUNCTION(this << address);

    if (InetSocketAddress::IsMatchingType(address) == true)
    {
        InetSocketAddress transport = InetSocketAddress::ConvertFrom(address);
        m_defaultAddress = Address(transport.GetIpv4());
        SetIpTos(transport.GetTos());
        m_connected = true;
        NotifyConnectionSucceeded();
    }
    else
    {
        NotifyConnectionFailed();
        return -1;
    }
    return 0;
}

int
UdpBasedSocket::Listen(void)
{
    m_errno = Socket::ERROR_OPNOTSUPP;
    return -1;
}

// maximum message size for UDP broadcast is limited by MTU
// size of underlying link; we are not checking that now.
// \todo Check MTU size of underlying link
uint32_t
UdpBasedSocket::GetTxAvailable(void) const
{
    NS_LOG_FUNCTION(this);
    // No finite send buffer is modelled, but we must respect
    // the maximum size of an IP datagram (65535 bytes - headers).
    return MAX_IPV4_UDP_DATAGRAM_SIZE;
}

int
UdpBasedSocket::Send(Ptr<Packet> p, uint32_t flags)
{
    NS_LOG_FUNCTION(this << p << flags);

    if (!m_connected)
    {
        m_errno = ERROR_NOTCONN;
        return -1;
    }
    return DoSend(p);
}

int
UdpBasedSocket::DoSend(Ptr<Packet> p)
{
    NS_LOG_FUNCTION(this << p);
    if ((m_endPoint == 0) && (Ipv4Address::IsMatchingType(m_defaultAddress) == true))
    {
        if (Bind() == -1)
        {
            NS_ASSERT(m_endPoint == 0);
            return -1;
        }
        NS_ASSERT(m_endPoint != 0);
    }
    if (m_shutdownSend)
    {
        m_errno = ERROR_SHUTDOWN;
        return -1;
    }
    if (Ipv4Address::IsMatchingType(m_defaultAddress))
    {
        // FIXME: maybe in the real world, we should check the size of the packet
        // if (p->GetSize() > GetTxAvailable())
        // {
        //     m_errno = ERROR_MSGSIZE;
        //     return -1;
        // }
        uint8_t tos = GetIpTos();
        uint8_t priority = GetPriority();
        if (tos)
        {
            // This tag will be used to set IP TOS field in L3
            SocketIpTosTag ipTosTag;
            ipTosTag.SetTos(tos);
            // This packet may already have a SocketIpTosTag (see BUG 2440)
            p->ReplacePacketTag(ipTosTag);
            priority = IpTos2Priority(tos);
        }

        if (priority)
        {
            SocketPriorityTag priorityTag;
            priorityTag.SetPriority(priority);
            p->ReplacePacketTag(priorityTag);
        }
        Ptr<Ipv4> ipv4 = m_node->GetObject<Ipv4>();
        // Not supporting:
        //   1. multicast
        //   2. manually set TTL
        //   3. specifying don't fragment
        //   4. broadcast
        if (ipv4->GetRoutingProtocol() != nullptr)
        {
            Ipv4Address daddr = Ipv4Address::ConvertFrom(m_defaultAddress);
            Ipv4Header header;
            header.SetDestination(daddr);
            header.SetProtocol(UdpL4Protocol::PROT_NUMBER);
            Socket::SocketErrno errno_;
            Ptr<Ipv4Route> route;
            Ptr<NetDevice> oif = m_boundnetdevice; // specify non-zero if bound to a specific device
            // TBD-- we could cache the route and just check its validity
            Ptr<Packet> routePacket = Create<Packet>();
            UdpHeader udpHeader;
            udpHeader.SetDestinationPort(m_endPoint->GetPeerPort());
            udpHeader.SetSourcePort(m_endPoint->GetLocalPort());
            routePacket->AddHeader(udpHeader);
            route = ipv4->GetRoutingProtocol()->RouteOutput(routePacket, header, oif, errno_);
            if (route != nullptr)
            {
                NS_LOG_LOGIC("Route exists");

                // Here we try to route subnet-directed broadcasts
                uint32_t outputIfIndex = ipv4->GetInterfaceForDevice(route->GetOutputDevice());
                uint32_t ifNAddr = ipv4->GetNAddresses(outputIfIndex);
                for (uint32_t addrI = 0; addrI < ifNAddr; ++addrI)
                {
                    Ipv4InterfaceAddress ifAddr = ipv4->GetAddress(outputIfIndex, addrI);
                    if (daddr == ifAddr.GetBroadcast())
                    {
                        m_errno = ERROR_OPNOTSUPP;
                        return -1;
                    }
                }
                DoSendTo(p, daddr, route);
                NotifyDataSent(p->GetSize());
                return p->GetSize();
            }
            else
            {
                NS_LOG_LOGIC("No route to destination");
                NS_LOG_ERROR(errno_);
                m_errno = errno_;
                return -1;
            }
        }
        else
        {
            NS_LOG_ERROR("ERROR_NOROUTETOHOST");
            m_errno = ERROR_NOROUTETOHOST;
            return -1;
        }
        return 0;
    }

    m_errno = ERROR_AFNOSUPPORT;
    return -1;
}

void
UdpBasedSocket::DoSendTo(Ptr<Packet> p, Ipv4Address daddr, Ptr<Ipv4Route> route)
{
    NS_LOG_FUNCTION(this << p << daddr);

    m_innerProto->Send(p->Copy(),
                       route->GetSource(),
                       daddr,
                       m_endPoint->GetLocalPort(),
                       m_endPoint->GetPeerPort(),
                       route);
}

void
UdpBasedSocket::NotifyFlowCompletes()
{
    NS_LOG_FUNCTION(this);

    if (!m_flowCompleteCallback.IsNull())
    {
        m_flowCompleteCallback(this);
    }
}

int
UdpBasedSocket::SendTo(Ptr<Packet> p, uint32_t flags, const Address& toAddress)
{
    NS_FATAL_ERROR("Should not call SendTo on UdpBasedSocket");
    return -1;
}

uint32_t
UdpBasedSocket::GetRxAvailable(void) const
{
    NS_LOG_FUNCTION(this);
    // We separately maintain this state to avoid walking the queue
    // every time this might be called
    return m_rxUsed;
}

Ptr<Packet>
UdpBasedSocket::Recv(uint32_t maxSize, uint32_t flags)
{
    NS_LOG_FUNCTION(this << maxSize << flags);

    Address fromAddress;
    Ptr<Packet> packet = RecvFrom(maxSize, flags, fromAddress);
    return packet;
}

Ptr<Packet>
UdpBasedSocket::RecvFrom(uint32_t maxSize, uint32_t flags, Address& fromAddress)
{
    NS_LOG_FUNCTION(this << maxSize << flags << fromAddress);

    if (m_deliveryQueue.empty())
    {
        return 0;
    }
    Ptr<Packet> p = m_deliveryQueue.front().first;
    fromAddress = m_deliveryQueue.front().second;

    if (p->GetSize() <= maxSize)
    {
        m_deliveryQueue.pop();
        m_rxUsed -= p->GetSize();
    }
    else
    {
        p = 0;
    }
    return p;
}

int
UdpBasedSocket::GetSockName(Address& address) const
{
    NS_LOG_FUNCTION(this << address);
    if (m_endPoint != 0)
    {
        address = InetSocketAddress(m_innerProto->GetLocalAddress(), m_endPoint->GetLocalPort());
    }
    else
    { // It is possible to call this method on a socket without a name
        // in which case, behavior is unspecified
        // Should this return an InetSocketAddress or an Inet6SocketAddress?
        address = InetSocketAddress(Ipv4Address::GetZero(), 0);
    }
    return 0;
}

int
UdpBasedSocket::GetPeerName(Address& address) const
{
    NS_LOG_FUNCTION(this << address);

    if (!m_connected)
    {
        m_errno = ERROR_NOTCONN;
        return -1;
    }

    if (Ipv4Address::IsMatchingType(m_defaultAddress))
    {
        Ipv4Address addr = Ipv4Address::ConvertFrom(m_defaultAddress);
        InetSocketAddress inet(addr, m_innerProto->GetProtocolNumber());
        inet.SetTos(GetIpTos());
        address = inet;
    }
    else
    {
        NS_ASSERT_MSG(false, "unexpected address type");
    }

    return 0;
}

bool
UdpBasedSocket::SetAllowBroadcast(bool allowBroadcast)
{
    if (allowBroadcast)
    {
        NS_FATAL_ERROR("Broadcast is not allowed on UdpBasedSocket for now");
    }
    return false;
}

bool
UdpBasedSocket::GetAllowBroadcast() const
{
    return false;
}

void
UdpBasedSocket::ForwardUp(Ptr<Packet> packet,
                          Ipv4Header header,
                          uint32_t port,
                          Ptr<Ipv4Interface> incomingInterface)
{
    NS_LOG_FUNCTION(this << packet << header << port);

    if (m_shutdownRecv)
    {
        return;
    }

    // Should check via getsockopt ()..
    if (IsRecvPktInfo())
    {
        Ipv4PacketInfoTag tag;
        packet->RemovePacketTag(tag);
        tag.SetAddress(header.GetDestination());
        tag.SetTtl(header.GetTtl());
        tag.SetRecvIf(incomingInterface->GetDevice()->GetIfIndex());
        packet->AddPacketTag(tag);
    }

    // Check only version 4 options
    if (IsIpRecvTos())
    {
        SocketIpTosTag ipTosTag;
        ipTosTag.SetTos(header.GetTos());
        packet->AddPacketTag(ipTosTag);
    }

    // in case the packet still has a priority tag attached, remove it
    SocketPriorityTag priorityTag;
    packet->RemovePacketTag(priorityTag);

    if ((m_rxUsed + packet->GetSize()) <= m_rcvBufSize)
    {
        Address address = InetSocketAddress(header.GetSource(), port);
        m_deliveryQueue.push(std::make_pair(packet, address));
        m_rxUsed += packet->GetSize();
        NotifyDataRecv();
    }
    else
    {
        // In general, this case should not occur unless the
        // receiving application reads data from this socket slowly
        // in comparison to the arrival rate
        //
        // drop and trace packet
        NS_LOG_WARN("UdpBasedSocket: No receive buffer space available on node "
                    << Simulator::GetContext() << ". Drop.");
        m_dropTrace(packet);
    }
}

void
UdpBasedSocket::FinishSending()
{
    NS_LOG_FUNCTION(this);
}

void
UdpBasedSocket::SetFlowCompleteCallback(Callback<void, Ptr<UdpBasedSocket>> cb)
{
    NS_LOG_FUNCTION(this);
    m_flowCompleteCallback = cb;
}

void
UdpBasedSocket::SetLocalAddress(Ipv4Address local)
{
    NS_LOG_FUNCTION(this);
    m_localAddress = local;
}

Ipv4Address
UdpBasedSocket::GetLocalAddress() const
{
    NS_LOG_FUNCTION(this);
    return m_localAddress;
}

void
UdpBasedSocket::SetPeerAddress(Ipv4Address peer)
{
    NS_LOG_FUNCTION(this);
    m_defaultAddress = Address(peer);
}

Ipv4Address
UdpBasedSocket::GetPeerAddress() const
{
    NS_LOG_FUNCTION(this);
    return Ipv4Address::ConvertFrom(m_defaultAddress);
}

uint32_t
UdpBasedSocket::GetSrcPort() const
{
    NS_LOG_FUNCTION(this);
    return m_endPoint->GetLocalPort();
}

uint32_t
UdpBasedSocket::GetDstPort() const
{
    NS_LOG_FUNCTION(this);
    return m_endPoint->GetPeerPort();
}

void
UdpBasedSocket::SetRcvBufSize(uint32_t size)
{
    m_rcvBufSize = size;
}

uint32_t
UdpBasedSocket::GetRcvBufSize(void) const
{
    return m_rcvBufSize;
}

/* class UdpBasedSocketFactory */
NS_OBJECT_ENSURE_REGISTERED(UdpBasedSocketFactory);

TypeId
UdpBasedSocketFactory::GetTypeId(void)
{
    static TypeId tid = TypeId("ns3::UdpBasedSocketFactory")
                            .SetParent<SocketFactory>()
                            .SetGroupName("Dcb")
                            .AddConstructor<UdpBasedSocketFactory>();
    return tid;
}

UdpBasedSocketFactory::UdpBasedSocketFactory()
{
}

UdpBasedSocketFactory::~UdpBasedSocketFactory()
{
}

uint32_t
UdpBasedSocketFactory::AddUdpBasedProtocol(Ptr<Node> node, Ptr<NetDevice> dev, TypeId protoTid)
{
    // Check if the protocol is already added
    Ptr<UdpBasedL4Protocol> protoObj = node->GetObject<UdpBasedL4Protocol>(protoTid);
    if (protoObj != nullptr)
    {
        // Already added, return the header size
        return protoObj->GetHeaderSize();
    }

    // Create the protocol and aggrate to the node
    ObjectFactory factory;
    factory.SetTypeId(protoTid);
    Ptr<UdpBasedL4Protocol> innerProto = DynamicCast<UdpBasedL4Protocol>(factory.Create());
    innerProto->Setup(node, dev);
    node->AggregateObject(innerProto);

    // Register a sendDataCb to the device if it is a DcbNetDevice and the proto is RoCEv2
    if (protoTid == RoCEv2L4Protocol::GetTypeId())
    {
        Ptr<RoCEv2L4Protocol> rocev2Proto = DynamicCast<RoCEv2L4Protocol>(innerProto);
        for (uint32_t idx = 1; idx < node->GetNDevices(); idx++)
        {
            Ptr<DcbNetDevice> dcbDev = DynamicCast<DcbNetDevice>(node->GetDevice(idx));
            if (dcbDev != nullptr)
            {
                Ptr<PausableQueueDisc> queueDisc = dcbDev->GetQueueDisc();
                queueDisc->RegisterSendDataCallback(
                    MakeCallback(&RoCEv2L4Protocol::NotifyCouldSend, rocev2Proto));
            }
        }
    }

    uint16_t udpPort = innerProto->GetProtocolNumber();
    auto proto = m_protoMapper.find(udpPort);
    if (proto == m_protoMapper.end())
    {
        m_protoMapper.emplace(udpPort, innerProto);
    }
    else
    {
        NS_LOG_WARN("Already added protocol on UDP port " << udpPort);
    }
    SetNextProtocol(udpPort);
    return innerProto->GetHeaderSize();
}

void
UdpBasedSocketFactory::SetNextProtocol(uint16_t port)
{
    auto it = m_protoMapper.find(port);
    if (it != m_protoMapper.end())
    {
        m_nextProto = it->second;
    }
    else
    {
        NS_FATAL_ERROR("Cannot find UDP-based protocol on UDP port " << port);
    }
}

Ptr<Socket>
UdpBasedSocketFactory::CreateSocket(void)
{
    return m_nextProto->CreateSocket();
}

} // namespace ns3
