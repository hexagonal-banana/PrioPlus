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
 */

#include "dcb-base-application.h"

#include "dcb-net-device.h"
#include "ndp-l4-protocol.h"
#include "ndp-socket.h"
#include "rocev2-dcqcn.h"
#include "rocev2-l4-protocol.h"
#include "rocev2-socket.h"
#include "udp-based-socket.h"

#include "ns3/boolean.h"
#include "ns3/double.h"
#include "ns3/enum.h"
#include "ns3/fatal-error.h"
#include "ns3/global-value.h"
#include "ns3/integer.h"
#include "ns3/ipv4-address.h"
#include "ns3/ipv4.h"
#include "ns3/log-macros-enabled.h"
#include "ns3/loopback-net-device.h"
#include "ns3/node.h"
#include "ns3/packet-socket-address.h"
#include "ns3/ptr.h"
#include "ns3/simulator.h"
#include "ns3/socket.h"
#include "ns3/string.h"
#include "ns3/tcp-socket-base.h"
#include "ns3/tcp-socket-factory.h"
#include "ns3/tcp-tx-buffer.h"
#include "ns3/tracer-extension.h"
#include "ns3/type-id.h"
#include "ns3/udp-l4-protocol.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/udp-socket.h"
#include "ns3/uinteger.h"

#include <cmath>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("DcbBaseApplication");
NS_OBJECT_ENSURE_REGISTERED(DcbBaseApplication);

TypeId
DcbBaseApplication::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::DcbBaseApplication")
            .SetParent<Application>()
            .SetGroupName("Dcb")
            // No constructor as this is an abstract class
            // .AddConstructor<DcbBaseApplication>()
            // .AddAttribute ("Protocol",
            //                "The type of protocol to use. This should be "
            //                "a subclass of ns3::SocketFactory",
            //                TypeIdValue (UdpSocketFactory::GetTypeId ()),
            //                MakeTypeIdAccessor (&DcbBaseApplication::m_socketTid),
            //                // This should check for SocketFactory as a parent
            //                MakeTypeIdChecker ())
            .AddAttribute("CongestionType",
                          "Socket' congestion control type.",
                          TypeIdValue(RoCEv2Dcqcn::GetTypeId()),
                          MakeTypeIdAccessor(&DcbBaseApplication::SetCongestionTypeId),
                          MakeTypeIdChecker())
            .AddAttribute("SendEnabled",
                          "Enable sending",
                          BooleanValue(true),
                          MakeBooleanAccessor(&DcbTrafficGenApplication::m_enableSend),
                          MakeBooleanChecker())
            .AddTraceSource("FlowComplete",
                            "Trace when a flow completes.",
                            MakeTraceSourceAccessor(&DcbBaseApplication::m_flowCompleteTrace),
                            "ns3::TracerExtension::FlowTracedCallback")
            .AddAttribute("FlowPriority",
                          "The priority of the flow",
                          UintegerValue(0),
                          MakeUintegerAccessor(&DcbTrafficGenApplication::m_flowPriority),
                          MakeUintegerChecker<uint8_t>())
            .AddAttribute("RecvPriority",
                          "The priority of the recv",
                          UintegerValue(0),
                          MakeUintegerAccessor(&DcbTrafficGenApplication::m_recvPriority),
                          MakeUintegerChecker<uint8_t>());
    return tid;
}

DcbBaseApplication::DcbBaseApplication()
    : m_totBytes(0),
      m_headerSize(8 + 20 + 14 + 2),
      m_topology(nullptr),
      m_nodeIndex(0),
      m_enableReceive(true),
      m_node(nullptr),
      m_ecnEnabled(true)
{
    NS_LOG_FUNCTION(this);

    m_stats = std::make_shared<Stats>(this);
}

DcbBaseApplication::DcbBaseApplication(Ptr<DcTopology> topology, uint32_t nodeIndex)
    : m_totBytes(0),
      m_headerSize(8 + 20 + 14 + 2),
      m_topology(topology),
      m_nodeIndex(nodeIndex),
      m_enableReceive(true),
      m_node(topology->GetNode(nodeIndex).nodePtr),
      m_ecnEnabled(true)
{
    NS_LOG_FUNCTION(this);

    m_stats = std::make_shared<Stats>(this);
    InitSocketLineRate();
}

DcbBaseApplication::~DcbBaseApplication()
{
    NS_LOG_FUNCTION(this);
}

void
DcbBaseApplication::SetTopologyAndNode(Ptr<DcTopology> topology, uint32_t nodeIndex)
{
    m_topology = topology;
    m_nodeIndex = nodeIndex;
    m_node = topology->GetNode(nodeIndex).nodePtr;

    InitSocketLineRate();
}

// int64_t
// DcbBaseApplication::AssignStreams (int64_t stream)
// {
//   NS_LOG_FUNCTION (this << stream);
// }

void
DcbBaseApplication::SetProtocolGroup(ProtocolGroup protoGroup)
{
    m_protoGroup = protoGroup;
    if (protoGroup == ProtocolGroup::TCP)
    {
        m_socketTid = TcpSocketFactory::GetTypeId();
        m_headerSize = 20 + 20 + 14 + 2;
    }
    else if (protoGroup == ProtocolGroup::NDP)
    {
        // IPv4 + Ethernet + NDP header (+2B padding to mirror existing accounting style)
        m_headerSize = 20 + 14 + 22 + 2;
        m_dataHeaderSize = m_headerSize;
    }
}

void
DcbBaseApplication::SetInnerUdpProtocol(std::string innerTid)
{
    SetInnerUdpProtocol(TypeId(innerTid));
}

void
DcbBaseApplication::SetInnerUdpProtocol(TypeId innerTid)
{
    NS_LOG_FUNCTION(this << innerTid);
    if (m_protoGroup != ProtocolGroup::RoCEv2)
    {
        NS_FATAL_ERROR("Inner UDP protocol should be used together with RoCEv2 protocol group.");
    }
    m_socketTid = UdpBasedSocketFactory::GetTypeId();
    Ptr<Node> node = GetNode();
    Ptr<UdpBasedSocketFactory> socketFactory = node->GetObject<UdpBasedSocketFactory>();
    if (socketFactory)
    {
        m_headerSize = socketFactory->AddUdpBasedProtocol(node, GetOutboundNetDevice(), innerTid);

        /**
         * XXX Calculate the data header size.
         * To implement this we need to create a congestion control algorithm object and get the
         * extra header size. It is not a good idea to create a congestion control algorithm object
         * here. Need to be improved.
         */
        ObjectFactory congestionAlgorithmFactory;
        congestionAlgorithmFactory.SetTypeId(m_congestionTypeId);
        Ptr<RoCEv2CongestionOps> algo = congestionAlgorithmFactory.Create<RoCEv2CongestionOps>();
        // Set attributes for the congestion control algorithm as attributes may affect header size
        for (const auto& [name, value] : m_ccAttributes)
        {
            algo->SetAttribute(name, *value);
        }
        m_dataHeaderSize = m_headerSize + algo->GetExtraHeaderSize();
    }
    else
    {
        NS_FATAL_ERROR("Application cannot use inner-UDP protocol because UdpBasedL4Protocol and "
                       "UdpBasedSocketFactory is not bound to node correctly.");
    }
}

void
DcbBaseApplication::InitSocketLineRate()
{
    if (DynamicCast<DcbNetDevice>(GetOutboundNetDevice()) != nullptr)
    {
        m_socketLinkRate = DynamicCast<DcbNetDevice>(GetOutboundNetDevice())->GetDataRate();
    }
    else
    {
        StringValue sdv;
        if (GlobalValue::GetValueByNameFailSafe("defaultRate", sdv))
            m_socketLinkRate = DataRate(sdv.Get());
        else
            NS_FATAL_ERROR(
                "traceApp's socket is not bound to a DcbNetDevice and no default rate is "
                "set.");
    }
}

void
DcbBaseApplication::SetupReceiverSocket()
{
    NS_LOG_FUNCTION(this);
    if (m_protoGroup == ProtocolGroup::RoCEv2)
    {
        // Check if a receiver socket has been created
        Ptr<RoCEv2L4Protocol> roceL4 = GetNode()->GetObject<RoCEv2L4Protocol>();
        if (!roceL4->CheckLocalPortExist(RoCEv2L4Protocol::DefaultServicePort()))
        {
            // crate a special socket to act as the receiver
            m_receiverSocket = Socket::CreateSocket(GetNode(), m_socketTid);
            m_receiverSocket->SetIpTos(m_recvPriority << 2);
            Ptr<RoCEv2Socket> roceSocket = DynamicCast<RoCEv2Socket>(m_receiverSocket);
            roceSocket->SetCcOps(m_congestionTypeId, m_ccAttributes);
            roceSocket->BindToNetDevice(GetOutboundNetDevice());
            roceSocket->BindToLocalPort(RoCEv2L4Protocol::DefaultServicePort());
            roceSocket->ShutdownSend();
            // Set stop time to max to avoid receiver socket close
            roceSocket->SetStopTime(Time::Max());
            // Mirror sender-side socket state so receiver-side CC has packet sizing info
            roceSocket->GetSocketState()->SetPacketSize(MSS + m_dataHeaderSize);
            roceSocket->GetSocketState()->SetMss(MSS);
            // Listener socket receives all incoming packets and forwards them to the app via
            // HandleRead.
            roceSocket->SetListenerMode(MakeCallback(&DcbBaseApplication::HandleRead, this));
            roceSocket->SetRecvCallback(MakeCallback(&DcbBaseApplication::HandleRead, this));
        }
    }
    else if (m_protoGroup == ProtocolGroup::NDP)
    {
        Ptr<NdpL4Protocol> ndpL4 = GetNode()->GetObject<NdpL4Protocol>();
        if (ndpL4 == nullptr)
        {
            NS_FATAL_ERROR("NDP L4 protocol is not installed on node " << GetNode()->GetId());
        }

        Ptr<NdpSocket> ndpSocket = ndpL4->CreateSocket();
        m_receiverSocket = ndpSocket;
        ndpSocket->Bind(InetSocketAddress(Ipv4Address::GetAny(), 4000));
        ndpSocket->SetRecvCallback(MakeCallback(&DcbBaseApplication::HandleRead, this));
        ndpSocket->SetAcceptCallback(MakeNullCallback<bool, Ptr<Socket>, const Address&>(),
                                     MakeCallback(&DcbBaseApplication::HandleNdpAccept, this));
        ndpSocket->Listen();
    }
    // TCP
    else if (m_protoGroup == ProtocolGroup::TCP)
    {
        m_receiverSocket = Socket::CreateSocket(GetNode(), m_socketTid);
        if (m_receiverSocket->Bind(InetSocketAddress(Ipv4Address::GetAny(), 200)) == -1)
        {
            NS_FATAL_ERROR("Failed to bind TCP socket");
        }
        m_receiverSocket->Listen();
        m_receiverSocket->ShutdownSend();
        // Set stop time to max to avoid receiver socket close
        m_receiverSocket->SetRecvCallback(MakeCallback(&DcbBaseApplication::HandleRead, this));
        m_receiverSocket->SetAcceptCallback(
            MakeNullCallback<bool, Ptr<Socket>, const Address&>(),
            MakeCallback(&DcbBaseApplication::HandleTcpAccept, this));
        m_receiverSocket->SetCloseCallbacks(
            MakeCallback(&DcbBaseApplication::HandleTcpPeerClose, this),
            MakeCallback(&DcbBaseApplication::HandleTcpPeerError, this));
    }
    else
    {
        NS_FATAL_ERROR("Receive is not supported for this protocol group");
    }
}

void
DcbBaseApplication::StartApplication(void)
{
    NS_LOG_FUNCTION(this);

    if (m_enableReceive)
    {
        SetupReceiverSocket();
    }

    if (m_enableSend)
    {
        InitMembers();
        CalcTrafficParameters();
        if (m_protoGroup == ProtocolGroup::NDP)
        {
            // NDP is receiver-driven but has no TCP-style handshake.
            // When sender/receiver apps share the same startTime on different nodes,
            // let all receivers finish binding/listening first to avoid dropping the
            // sender's initial push window at t=0.
            Simulator::Schedule(NanoSeconds(1), &DcbBaseApplication::GenerateTraffic, this);
        }
        else
        {
            GenerateTraffic();
        }
    }
}

void
DcbBaseApplication::StopApplication(void)
{
    NS_LOG_FUNCTION(this);
}

InetSocketAddress
DcbBaseApplication::NodeIndexToAddr(uint32_t destNode) const
{
    NS_LOG_FUNCTION(this);

    uint32_t portNum = 0;
    switch (m_protoGroup)
    {
    case ProtocolGroup::RAW_UDP:
        NS_FATAL_ERROR("UDP port has not been chosen");
        break;
    case ProtocolGroup::TCP:
        portNum = 200; // FIXME not raw
        break;
    case ProtocolGroup::RoCEv2:
        portNum = RoCEv2L4Protocol::DefaultServicePort();
        break;
    case ProtocolGroup::NDP:
        portNum = 4000;
        break;
    }

    // 0 interface is LoopbackNetDevice
    uint32_t idx = rand() % (m_topology->GetNode(destNode)->GetNDevices() - 1);
    Ipv4Address ipv4Addr =
        m_topology->GetInterfaceOfNode(destNode, idx + 1)
            .GetAddress(); // liuchangTODO: need randomly choose a destAddr in dstNode
    return InetSocketAddress(ipv4Addr, portNum);
}

Ptr<Socket>
DcbBaseApplication::CreateNewSocket(uint32_t destNode, uint32_t priority)
{
    NS_LOG_FUNCTION(this);
    if (m_protoGroup == ProtocolGroup::NDP)
    {
        Ptr<NdpL4Protocol> ndpL4 = GetNode()->GetObject<NdpL4Protocol>();
        if (ndpL4 == nullptr)
        {
            NS_FATAL_ERROR("NDP L4 protocol is not installed on node " << GetNode()->GetId());
        }

        Ptr<NdpSocket> ndpSocket = ndpL4->CreateSocket();
        for (const auto& [name, value] : m_socketAttributes)
        {
            ndpSocket->SetAttribute(name, *value);
        }

        std::vector<Ipv4Address> paths;
        Ptr<Node> destNodePtr = m_topology->GetNode(destNode).nodePtr;
        Ptr<Ipv4> destIpv4 = destNodePtr->GetObject<Ipv4>();
        if (destIpv4 != nullptr)
        {
            for (uint32_t ifIdx = 1; ifIdx < destIpv4->GetNInterfaces(); ++ifIdx)
            {
                Ipv4Address pathIp = destIpv4->GetAddress(ifIdx, 0).GetLocal();
                if (pathIp != Ipv4Address::GetZero())
                {
                    paths.push_back(pathIp);
                }
            }
        }
        if (paths.empty())
        {
            NS_FATAL_ERROR("Destination node " << destNode << " has no usable IPv4 path for NDP");
        }

        ndpSocket->SetPaths(paths);
        if (ndpSocket->Bind() == -1)
        {
            NS_FATAL_ERROR("Failed to bind NDP socket");
        }
        ndpSocket->SetFlowCompleteCallback(MakeCallback(
            static_cast<void (DcbBaseApplication::*)(Ptr<NdpSocket>)>(
                &DcbBaseApplication::FlowCompletes),
            this));

        InetSocketAddress destAddr(paths.front(), 4000);
        if (ndpSocket->Connect(destAddr) == -1)
        {
            NS_FATAL_ERROR("NDP socket connection failed");
        }

        Ptr<Socket> socket = ndpSocket;
        socket->SetRecvCallback(MakeCallback(&DcbBaseApplication::HandleRead, this));
        return socket;
    }

    InetSocketAddress destAddr = NodeIndexToAddr(destNode);
    return CreateNewSocket(destAddr, priority);
}

Ptr<Socket>
DcbBaseApplication::CreateNewSocket(InetSocketAddress destAddr, uint32_t priority)
{
    NS_LOG_FUNCTION(this);

    Ptr<Socket> socket;
    int ret = 0;
    uint32_t outDevIdx = 0;

    if (m_protoGroup == ProtocolGroup::NDP)
    {
        Ptr<NdpL4Protocol> ndpL4 = GetNode()->GetObject<NdpL4Protocol>();
        if (ndpL4 == nullptr)
        {
            NS_FATAL_ERROR("NDP L4 protocol is not installed on node " << GetNode()->GetId());
        }
        Ptr<NdpSocket> ndpSocket = ndpL4->CreateSocket();
        socket = ndpSocket;

        ndpSocket->SetPaths({destAddr.GetIpv4()});

        for (const auto& [name, value] : m_socketAttributes)
        {
            ndpSocket->SetAttribute(name, *value);
        }

        ret = ndpSocket->Bind();
        if (ret == -1)
        {
            NS_FATAL_ERROR("Failed to bind NDP socket");
        }
        ndpSocket->SetFlowCompleteCallback(MakeCallback(
            static_cast<void (DcbBaseApplication::*)(Ptr<NdpSocket>)>(
                &DcbBaseApplication::FlowCompletes),
            this));
    }
    else
    {
        // The InstanceTypeId of socket is RoCEv2Socket or TcpSocket
        socket = Socket::CreateSocket(GetNode(), m_socketTid);
        socket->SetIpTos(priority << 2);

        ret = socket->Bind();
        if (ret == -1)
        {
            NS_FATAL_ERROR("Failed to bind socket");
        }
    }
    uint32_t srcPort = 0;
    uint32_t dstPort = destAddr.GetPort();
    if (m_protoGroup == ProtocolGroup::RoCEv2)
    {
        Ptr<RoCEv2Socket> roceV2Socket = DynamicCast<RoCEv2Socket>(socket);
        NS_ASSERT(roceV2Socket);
        srcPort = roceV2Socket->GetSrcPort();
        dstPort = roceV2Socket->GetDstPort();
    }
    else if (m_protoGroup == ProtocolGroup::TCP)
    {
        Ptr<TcpSocketBase> tcpSocket = DynamicCast<TcpSocketBase>(socket);
        NS_ASSERT(tcpSocket);
        Address address;
        tcpSocket->GetSockName(address);
        srcPort = InetSocketAddress::ConvertFrom(address).GetPort();
        // tcpSocket->GetPeerName(address);
        // dstPort = InetSocketAddress::ConvertFrom(address).GetPort();
    }
    if (m_protoGroup != ProtocolGroup::NDP)
    {
        outDevIdx = m_topology->GetOutDevIdx(GetNode(), destAddr.GetIpv4(), srcPort, dstPort);
        Ptr<NetDevice> outDev = GetNode()->GetDevice(outDevIdx);
        socket->BindToNetDevice(outDev);
    }

    if (m_ecnEnabled && m_protoGroup != ProtocolGroup::NDP)
    {
        // The low 2-bits of TOS field is ECN field.
        // The Tos of a flow is setted here.
        destAddr.SetTos((socket->GetIpTos() & 0xfc) | Ipv4Header::EcnType::ECN_ECT1);
    }
    ret = socket->Connect(destAddr);
    if (ret == -1)
    {
        NS_FATAL_ERROR("Socket connection failed");
    }

    if (m_protoGroup == ProtocolGroup::RoCEv2)
    {
        Ptr<UdpBasedSocket> udpBasedSocket = DynamicCast<UdpBasedSocket>(socket);
        if (udpBasedSocket)
        {
            udpBasedSocket->SetFlowCompleteCallback(
                MakeCallback(static_cast<void (DcbBaseApplication::*)(Ptr<UdpBasedSocket>)>(
                                 &DcbBaseApplication::FlowCompletes),
                             this));
            Ptr<RoCEv2Socket> roceSocket = DynamicCast<RoCEv2Socket>(udpBasedSocket);
            if (roceSocket)
            {
                // Set stop time to max to avoid sender socket's timer close
                roceSocket->SetCcOps(m_congestionTypeId, m_ccAttributes);
                roceSocket->SetStopTime(Time::Max());
                // Set socket attributes in m_appAttributes
                for (const auto& [name, value] : m_socketAttributes)
                {
                    roceSocket->SetAttribute(name, *value);
                }

                // calculate baseRTT and ack's payload size
                uint32_t ackHeaderSize =
                    m_headerSize + 4; // ack header has AETHeader, which is 4 bytes

                ackHeaderSize += roceSocket->GetCcOps()
                                     ->GetExtraAckSize(); // ack may be added by congestion control

                roceSocket->GetSocketState()->SetPacketSize(MSS + m_dataHeaderSize);
                roceSocket->GetSocketState()->SetMss(MSS);
                // ack size should be at least 64B
                uint32_t ackPayload = ackHeaderSize < 64 ? (64 - ackHeaderSize) : 0;

                Ipv4Address srcIpAddr =
                    m_topology->GetInterfaceOfNode(m_nodeIndex, outDevIdx).GetAddress();
                Ipv4Address destIpAddr = destAddr.GetIpv4();

                roceSocket->SetBaseRttNOneWayDelay(m_topology->GetHops(srcIpAddr, destIpAddr),
                                                   m_topology->GetDelay(srcIpAddr, destIpAddr),
                                                   MSS + m_headerSize,
                                                   ackPayload + ackHeaderSize);
                // Should not call SetReady here as the flow may not start now
            }
        }
    }

    socket->SetAllowBroadcast(false);
    // m_socket->SetConnectCallback (MakeCallback (&DcbBaseApplication::ConnectionSucceeded,
    // this),
    //                               MakeCallback (&DcbBaseApplication::ConnectionFailed,
    //                               this));
    // Every per-flow socket delivers payloads back to the application through the same
    // HandleRead callback.
    socket->SetRecvCallback(MakeCallback(&DcbBaseApplication::HandleRead, this));

    return socket;
}

void
DcbBaseApplication::SendNextPacket(Flow* flow)
{
    SendNextPacketWithTags(flow, {});
}

void
DcbBaseApplication::SendNextPacketWithTags(Flow* flow, std::vector<std::shared_ptr<Tag>> tags)
{
    // SetReady for RoCEv2Socket, this will start the congestion control
    // Note that for rocev2Socket, this function will only be called once
    Ptr<RoCEv2Socket> roceSocket = DynamicCast<RoCEv2Socket>(flow->socket);
    if (roceSocket)
    {
        roceSocket->GetCcOps()->SetReady();
        Ptr<Packet> packet = Create<Packet>(flow->remainBytes);
        // Add tags to the packet
        for (auto tag : tags)
        {
            packet->AddPacketTag(*tag);
        }
        flow->socket->Send(packet);
        m_totBytes += flow->remainBytes;
        flow->remainBytes = 0;
        return;
    }

    Ptr<NdpSocket> ndpSocket = DynamicCast<NdpSocket>(flow->socket);
    if (ndpSocket)
    {
        // Suppress premature completion while the application is still queueing packets.
        auto ndpStats = ndpSocket->GetStats();
        ndpStats->tStart = flow->startTime;
        ndpStats->nTotalSizeBytes = flow->totalBytes;
        ndpStats->nTotalSizePkts = (flow->totalBytes + MSS - 1) / MSS;
        ndpSocket->SetMoreDataPending(true);
    }

    while (flow->remainBytes != 0)
    {
        const uint32_t packetSize = std::min(flow->remainBytes, MSS);
        Ptr<Packet> packet = Create<Packet>(packetSize);
        // Add tags to the packet
        for (auto tag : tags)
        {
            packet->AddPacketTag(*tag);
        }
        int actual = flow->socket->Send(packet);
        if (actual == static_cast<int>(packetSize))
        {
            m_totBytes += packetSize;
            flow->remainBytes -= actual;

            Ptr<UdpSocket> udpSock = DynamicCast<UdpSocket>(flow->socket);
            if (udpSock != nullptr)
            {
                // For UDP socket, pacing the sending at application layer
                Time txTime = m_socketLinkRate.CalculateBytesTxTime(packetSize + m_dataHeaderSize);
                Simulator::Schedule(txTime,
                                    &DcbBaseApplication::SendNextPacketWithTags,
                                    this,
                                    flow,
                                    tags);
                return;
            }
        }
        else
        {
            // Typically this is because TCP socket's txBuffer is full, retry later.
            Time txTime = m_socketLinkRate.CalculateBytesTxTime(packetSize + m_dataHeaderSize);
            Simulator::Schedule(txTime,
                                &DcbBaseApplication::SendNextPacketWithTags,
                                this,
                                flow,
                                tags);
            return;
        }
    }

    if (ndpSocket)
    {
        ndpSocket->SetMoreDataPending(false);
    }

    // flow sending completes for RoCEv2Socket
    Ptr<UdpBasedSocket> udpSock = DynamicCast<UdpBasedSocket>(flow->socket);
    if (udpSock)
    {
        udpSock->FinishSending();
    }
}

void
DcbBaseApplication::SetEcnEnabled(bool enabled)
{
    NS_LOG_FUNCTION(this << enabled);
    m_ecnEnabled = enabled;
}

void
DcbBaseApplication::SetCcOpsAttributes(
    const std::vector<RoCEv2CongestionOps::CcOpsConfigPair_t>& configs)
{
    NS_LOG_FUNCTION(this);
    m_ccAttributes = configs;
}

void
DcbBaseApplication::ConnectionSucceeded(Ptr<Socket> socket)
{
    NS_LOG_FUNCTION(this << socket);
}

void
DcbBaseApplication::ConnectionFailed(Ptr<Socket> socket)
{
    NS_LOG_FUNCTION(this << socket);
}

void
DcbBaseApplication::HandleRead(Ptr<Socket> socket)
{
    NS_LOG_FUNCTION(this << socket);
    Ptr<Packet> packet;
    Address from;
    // Address localAddress;
    while ((packet = socket->RecvFrom(from)))
    {
        if (InetSocketAddress::IsMatchingType(from))
        {
            NS_LOG_LOGIC("DcbBaseApplication: At time "
                         << Simulator::Now().As(Time::S) << " client received " << packet->GetSize()
                         << " bytes from " << InetSocketAddress::ConvertFrom(from).GetIpv4()
                         << " port " << InetSocketAddress::ConvertFrom(from).GetPort());
        }
        else if (Inet6SocketAddress::IsMatchingType(from))
        {
            NS_LOG_LOGIC("DcbBaseApplication: At time "
                         << Simulator::Now().As(Time::S) << " client received " << packet->GetSize()
                         << " bytes from " << Inet6SocketAddress::ConvertFrom(from).GetIpv6()
                         << " port " << Inet6SocketAddress::ConvertFrom(from).GetPort());
        }
        // socket->GetSockName (localAddress);
        // m_rxTrace (packet);
        // m_rxTraceWithAddresses (packet, from, localAddress);
    }
}

void
DcbBaseApplication::FlowCompletes(Ptr<UdpBasedSocket> socket)
{
    auto p = m_flows.find(socket);
    if (p == m_flows.end())
    {
        NS_FATAL_ERROR("Cannot find socket in this application on node "
                       << Simulator::GetContext());
    }
    Flow* flow = p->second;
    m_flowCompleteTrace(Simulator::GetContext(),
                        flow->destNode,
                        socket->GetSrcPort(),
                        socket->GetDstPort(),
                        flow->totalBytes,
                        flow->startTime,
                        Simulator::Now());
}

void
DcbBaseApplication::FlowCompletes(Ptr<NdpSocket> socket)
{
    Ptr<Socket> baseSocket = socket;
    auto p = m_flows.find(baseSocket);
    if (p == m_flows.end())
    {
        NS_FATAL_ERROR("Cannot find NDP socket in this application on node "
                       << Simulator::GetContext());
    }

    Flow* flow = p->second;
    flow->finishTime = Simulator::Now();
    auto ndpStats = socket->GetStats();
    if (!ndpStats->tStart.IsStrictlyPositive())
    {
        ndpStats->tStart = flow->startTime;
    }
    ndpStats->tFinish = flow->finishTime;
    ndpStats->nTotalSizeBytes = flow->totalBytes;
    ndpStats->nTotalSizePkts = (flow->totalBytes + MSS - 1) / MSS;

    Address localAddress;
    Address peerAddress;
    socket->GetSockName(localAddress);
    socket->GetPeerName(peerAddress);

    uint32_t srcPort = 0;
    uint32_t dstPort = 0;
    if (InetSocketAddress::IsMatchingType(localAddress))
    {
        srcPort = InetSocketAddress::ConvertFrom(localAddress).GetPort();
    }
    if (InetSocketAddress::IsMatchingType(peerAddress))
    {
        dstPort = InetSocketAddress::ConvertFrom(peerAddress).GetPort();
    }

    m_flowCompleteTrace(Simulator::GetContext(),
                        flow->destNode,
                        srcPort,
                        dstPort,
                        flow->totalBytes,
                        flow->startTime,
                        flow->finishTime);
}

void
DcbBaseApplication::TcpFlowEnds(Flow* flow, SequenceNumber32 oldValue, SequenceNumber32 newValue)
{
    if (newValue.GetValue() >= flow->totalBytes)
    {
        flow->finishTime = Simulator::Now();
    }
}

void
DcbBaseApplication::SetSendEnabled(bool enabled)
{
    m_enableSend = enabled;
}

void
DcbBaseApplication::SetReceiveEnabled(bool enabled)
{
    m_enableReceive = enabled;
}

void
DcbBaseApplication::SetSocketAttributes(
    const std::vector<DcbBaseApplication::ConfigEntry_t>& socketAttributes)
{
    m_socketAttributes = socketAttributes;
}

// TODO TCP callback Handler
void
DcbBaseApplication::HandleTcpAccept(Ptr<Socket> socket, const Address& from)
{
    NS_LOG_FUNCTION(this << socket << from);
    socket->SetRecvCallback(MakeCallback(&DcbBaseApplication::HandleRead, this));
    m_acceptedSocketList.push_back(socket);
}

void
DcbBaseApplication::HandleNdpAccept(Ptr<Socket> socket, const Address& from)
{
    NS_LOG_FUNCTION(this << socket << from);
    socket->SetRecvCallback(MakeCallback(&DcbBaseApplication::HandleRead, this));
    m_acceptedSocketList.push_back(socket);
}

void
DcbBaseApplication::HandleTcpPeerClose(Ptr<Socket> socket)
{
    // Here to calculate the fct
}

void
DcbBaseApplication::HandleTcpPeerError(Ptr<Socket> socket)
{
}

Ptr<NetDevice>
DcbBaseApplication::GetOutboundNetDevice()
{
    // We do not use GetNode ()->GetDevice (0) as it is inavlid when the application is created
    Ptr<NetDevice> boundDev = m_node->GetDevice(0);
    if (DynamicCast<LoopbackNetDevice>(boundDev) != nullptr)
    {
        // Try to get the second net device as the first one may be loopback
        boundDev = m_node->GetDevice(1);
    }
    return boundDev;
}

void
DcbBaseApplication::SetFlowIdentifier(Flow* flow, Ptr<Socket> socket)
{
    if (m_protoGroup == ProtocolGroup::RoCEv2)
    {
        Ptr<RoCEv2Socket> roceSocket = DynamicCast<RoCEv2Socket>(socket);
        if (roceSocket != nullptr)
        {
            Ipv4Address srcAddr = roceSocket->GetLocalAddress();
            Ipv4Address dstAddr = roceSocket->GetPeerAddress();
            uint32_t srcQP = roceSocket->GetSrcPort();
            uint32_t dstQP = roceSocket->GetDstPort();
            flow->flowIdentifier = FlowIdentifier(srcAddr, dstAddr, srcQP, dstQP);
        }
        else
        {
            NS_FATAL_ERROR("Socket is not a RoCEv2 socket");
        }
    }
    else if (m_protoGroup == ProtocolGroup::TCP)
    {
        Ptr<TcpSocketBase> tcpSocket = DynamicCast<TcpSocketBase>(socket);
        NS_ASSERT(tcpSocket);
        Address address;
        tcpSocket->GetSockName(address);
        Ipv4Address srcAddr = InetSocketAddress::ConvertFrom(address).GetIpv4();
        uint32_t srcPort = InetSocketAddress::ConvertFrom(address).GetPort();
        tcpSocket->GetPeerName(address);
        Ipv4Address dstAddr = InetSocketAddress::ConvertFrom(address).GetIpv4();
        uint32_t dstPort = InetSocketAddress::ConvertFrom(address).GetPort();
        flow->flowIdentifier = FlowIdentifier(srcAddr, dstAddr, srcPort, dstPort);
    }
    else if (m_protoGroup == ProtocolGroup::NDP)
    {
        Address local;
        Address peer;
        socket->GetSockName(local);
        socket->GetPeerName(peer);
        if (InetSocketAddress::IsMatchingType(local) && InetSocketAddress::IsMatchingType(peer))
        {
            InetSocketAddress localAddr = InetSocketAddress::ConvertFrom(local);
            InetSocketAddress peerAddr = InetSocketAddress::ConvertFrom(peer);
            flow->flowIdentifier = FlowIdentifier(localAddr.GetIpv4(),
                                                  peerAddr.GetIpv4(),
                                                  localAddr.GetPort(),
                                                  peerAddr.GetPort());
        }
        else
        {
            NS_FATAL_ERROR("Socket is not an IPv4 NDP socket");
        }
    }
    else
    {
        NS_LOG_WARN("Flow identifier is not supported for this protocol group");
    }
}

void
DcbBaseApplication::SetCongestionTypeId(TypeId congestionTypeId)
{
    m_congestionTypeId = congestionTypeId;
}

DcbBaseApplication::Stats::Stats(Ptr<DcbBaseApplication> app)
    : m_app(app),
      isCollected(false),
      nTotalSizePkts(0),
      nTotalSizeBytes(0),
      nTotalSentPkts(0),
      nTotalSentBytes(0),
      nTotalDeliverPkts(0),
      nTotalDeliverBytes(0),
      nRetxCount(0),
      tStart(Time::Max()),
      tFinish(Time::Min()),
      overallRate(DataRate(0))
{
    // Retrieve the global config values
    BooleanValue bv;
    if (GlobalValue::GetValueByNameFailSafe("detailedSenderStats", bv))
        bDetailedSenderStats = bv.Get();
    else
        bDetailedSenderStats = false;
    if (GlobalValue::GetValueByNameFailSafe("detailedRetxStats", bv))
        bDetailedRetxStats = bv.Get();
    else
        bDetailedRetxStats = false;
}

void
DcbBaseApplication::Stats::CollectAndCheck(std::map<Ptr<Socket>, Flow*> flows)
{
    // Avoid collecting stats twice
    if (isCollected)
    {
        return;
    }
    isCollected = true;

    // Collect the statistics
    for (auto [socket, flow] : flows)
    {
        if (m_app->GetProtoGroup() == ProtocolGroup::RoCEv2)
        {
            Ptr<RoCEv2Socket> roceSocket = DynamicCast<RoCEv2Socket>(socket);
            if (roceSocket != nullptr)
            {
                auto roceStats = roceSocket->GetStats();
                nTotalSizePkts += roceStats->nTotalSizePkts;
                nTotalSizeBytes += roceStats->nTotalSizeBytes;
                nTotalSentPkts += roceStats->nTotalSentPkts;
                nTotalSentBytes += roceStats->nTotalSentBytes;
                nTotalDeliverPkts += roceStats->nTotalDeliverPkts;
                nTotalDeliverBytes += roceStats->nTotalDeliverBytes;
                nRetxCount += roceStats->nRetxCount;
                tStart = std::min(tStart, roceStats->tStart);
                tFinish = std::max(tFinish, roceStats->tFinish);

                mFlowStats[flow->flowIdentifier] = roceStats;
                vFlowStats.push_back(roceStats);
            }
        }
        else if (m_app->GetProtoGroup() == ProtocolGroup::TCP)
        {
            nTotalSizePkts += (flow->totalBytes + MSS - 1) / MSS;
            nTotalSizeBytes += flow->totalBytes;
            tStart = std::min(tStart, flow->startTime);
            tFinish = std::max(tFinish, flow->finishTime);

            std::shared_ptr<RoCEv2Socket::Stats> roceStats =
                std::make_shared<RoCEv2Socket::Stats>();
            roceStats->nTotalSizePkts = (flow->totalBytes + MSS - 1) / MSS;
            roceStats->nTotalSizeBytes = flow->totalBytes;
            roceStats->tStart = flow->startTime;
            roceStats->tFinish = flow->finishTime;
            roceStats->tFct = flow->finishTime - flow->startTime;
            roceStats->overallFlowRate =
                DataRate(roceStats->nTotalSizeBytes * 8.0 / roceStats->tFct.GetSeconds());

            mFlowStats[flow->flowIdentifier] = roceStats;
            vFlowStats.push_back(roceStats);
        }
        else if (m_app->GetProtoGroup() == ProtocolGroup::NDP)
        {
            Ptr<NdpSocket> ndpSocket = DynamicCast<NdpSocket>(socket);
            if (ndpSocket != nullptr)
            {
                auto ndpStats = ndpSocket->GetStats();
                ndpStats->CollectAndCheck();

                nTotalSizePkts += ndpStats->nTotalSizePkts;
                nTotalSizeBytes += ndpStats->nTotalSizeBytes;
                nTotalSentPkts += ndpStats->nTotalSentPkts;
                nTotalSentBytes += ndpStats->nTotalSentBytes;
                nRetxCount += ndpStats->nRetxCount;
                tStart = std::min(tStart, ndpStats->tStart);
                tFinish = std::max(tFinish, ndpStats->tFinish);

                std::shared_ptr<RoCEv2Socket::Stats> roceStats =
                    std::make_shared<RoCEv2Socket::Stats>();
                roceStats->nTotalSizePkts = ndpStats->nTotalSizePkts;
                roceStats->nTotalSizeBytes = ndpStats->nTotalSizeBytes;
                roceStats->nTotalSentPkts = ndpStats->nTotalSentPkts;
                roceStats->nTotalSentBytes = ndpStats->nTotalSentBytes;
                roceStats->nRetxCount = ndpStats->nRetxCount;
                roceStats->tStart = ndpStats->tStart;
                roceStats->tFinish = ndpStats->tFinish;
                roceStats->tFct = ndpStats->tFct;
                roceStats->overallFlowRate = ndpStats->overallFlowRate;
                roceStats->flowTag = ndpStats->flowTag;
                roceStats->bDetailedSenderStats = ndpStats->bDetailedSenderStats;
                roceStats->bDetailedRetxStats = ndpStats->bDetailedRetxStats;
                roceStats->vSentPkt = ndpStats->vSentPkt;

                for (const auto& [time, seq] : ndpStats->vRecvAck)
                {
                    roceStats->vExpectedPsn.emplace_back(time, seq);
                }
                for (const auto& [time, seq] : ndpStats->vRecvNack)
                {
                    roceStats->vAckedPsn.emplace_back(time, seq);
                }

                mFlowStats[flow->flowIdentifier] = roceStats;
                vFlowStats.push_back(roceStats);
            }
        }
    }
    // Calculate the overall rate
    overallRate = DataRate(nTotalSizeBytes * 8.0 / (tFinish - tStart).GetSeconds());
}

} // namespace ns3
