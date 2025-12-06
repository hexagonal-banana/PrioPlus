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

#include "rocev2-l4-protocol.h"

#include "dcb-net-device.h"
#include "pausable-queue-disc.h"
#include "rocev2-socket.h"

#include "ns3/ipv4-end-point.h"
#include "ns3/log.h"
#include "ns3/node.h"
#include "ns3/nstime.h"
#include "ns3/rocev2-header.h"
#include "ns3/simulator.h"
#include "ns3/type-id.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("RoCEv2L4Protocol");

NS_OBJECT_ENSURE_REGISTERED(RoCEv2L4Protocol);

TypeId
RoCEv2L4Protocol::GetTypeId(void)
{
    static TypeId tid = TypeId("ns3::RoCEv2L4Protocol")
                            .SetParent<UdpBasedL4Protocol>()
                            .SetGroupName("Dcb")
                            .AddConstructor<RoCEv2L4Protocol>();
    return tid;
}

RoCEv2L4Protocol::RoCEv2L4Protocol()
{
    NS_LOG_FUNCTION(this);
    m_innerEndPoints = new InnerEndPointDemux(1, 0xffff);

    // InnerEndPoint *endPoint =
    //     m_innerEndPoints->Allocate (GetDefaultServicePort (), 0); // as a receive service
    // endPoint->SetRxCallback (MakeCallback (&RoCEv2L4Protocol::ServerReceive, this));
}

RoCEv2L4Protocol::~RoCEv2L4Protocol()
{
    NS_LOG_FUNCTION(this);
}

Ptr<Socket>
RoCEv2L4Protocol::CreateSocket()
{
    NS_LOG_FUNCTION(this);
    Ptr<RoCEv2Socket> socket = CreateObject<RoCEv2Socket>();
    socket->SetNode(m_node);
    socket->SetInnerUdpProtocol(this);
    m_sockets.emplace_back(socket);
    return socket;
}

void
RoCEv2L4Protocol::FinishSetup(Ipv4EndPoint* const udpEndPoint)
{
    NS_LOG_FUNCTION(this);

    if (udpEndPoint != 0)
    {
        udpEndPoint->SetRxCallback(MakeCallback(&RoCEv2L4Protocol::ForwardUp, this));
        udpEndPoint->SetDestroyCallback(MakeCallback(&RoCEv2L4Protocol::Destroy, this));
    }
    else
    {
        NS_FATAL_ERROR("No UDP endpoint allocated to me.");
    }
}

uint16_t
RoCEv2L4Protocol::GetProtocolNumber(void) const
{
    return PROT_NUMBER;
}

uint32_t
RoCEv2L4Protocol::GetInnerProtocolHeaderSize() const
{
    static uint32_t sz = 0;
    if (sz == 0)
    {
        RoCEv2Header rocev2Header;
        sz = rocev2Header.GetSerializedSize();
    }
    return sz;
}

uint32_t
RoCEv2L4Protocol::GetHeaderSize() const
{
    static uint32_t sz = 8 + 20 + 14 + GetInnerProtocolHeaderSize(); // UDP + IP + Eth + RoCEv2
    return sz;
}

uint32_t
RoCEv2L4Protocol::GetDefaultServicePort() const
{
    return DEFAULT_DST_QP;
}

UdpBasedL4Protocol::InnerPortInfo
RoCEv2L4Protocol::ParseInnerPorts(Ptr<Packet> packet,
                                  Ipv4Header header,
                                  uint16_t port,
                                  Ptr<Ipv4Interface> incomingIntf)
{
    RoCEv2Header rocev2Header;
    packet->PeekHeader(rocev2Header);
    InnerPortInfo info{};
    info.dstPort = rocev2Header.GetDestQP();
    info.srcPort = rocev2Header.GetSrcQP();
    info.srcAddr = header.GetSource();
    return info;
}

uint32_t
RoCEv2L4Protocol::DefaultServicePort()
{
    return DEFAULT_DST_QP;
}

// InnerEndPoint *
// RoCEv2L4Protocol::Allocate ()
// {
//   uint32_t sport = m_innerEndPoints->AllocateEphemeralPort();
//   uint32_t dport = sport + ;
// }

InnerEndPoint*
RoCEv2L4Protocol::Allocate(uint32_t dstPort)
{
    NS_LOG_FUNCTION(this << dstPort);
    return m_innerEndPoints->Allocate(dstPort);
}

InnerEndPoint*
RoCEv2L4Protocol::Allocate(uint32_t srcPort, uint32_t dstPort)
{
    NS_LOG_FUNCTION(this << srcPort << dstPort);
    return m_innerEndPoints->Allocate(srcPort, dstPort);
}

InnerEndPoint*
RoCEv2L4Protocol::AllocateFlow(uint32_t dstPort, Ipv4Address srcAddr, uint32_t srcPort)
{
    NS_LOG_FUNCTION(this << dstPort << srcAddr << srcPort);
    return m_innerEndPoints->AllocateForFlow(dstPort, srcAddr, srcPort);
}

InnerEndPoint*
RoCEv2L4Protocol::LookupFlow(uint32_t dstPort, Ipv4Address srcAddr, uint32_t srcPort)
{
    return m_innerEndPoints->Lookup(dstPort, srcAddr, srcPort);
}

void
RoCEv2L4Protocol::DeAllocateFlow(uint32_t dstPort, Ipv4Address srcAddr, uint32_t srcPort)
{
    m_innerEndPoints->DeAllocateFlow(dstPort, srcAddr, srcPort);
}

bool
RoCEv2L4Protocol::CheckLocalPortExist(uint32_t localPort)
{
    NS_LOG_FUNCTION(this << localPort);
    return m_innerEndPoints->LookupPortLocal(localPort);
}

// void
// RoCEv2L4Protocol::ServerReceive (Ptr<Packet> packet, Ipv4Header header, uint32_t port,
//                                  Ptr<Ipv4Interface> incommingInterface)
// {
//   NS_LOG_FUNCTION (this << packet);

//   RoCEv2Header rocev2Header;
//   packet->RemoveHeader (rocev2Header);
//   RoCEv2Header::Opcode opcode = rocev2Header.GetOpcode ();
//   switch (opcode)
//     {
//     case RoCEv2Header::Opcode::RC_SEND_ONLY:
//       [[fallthrough]];
//     case RoCEv2Header::Opcode::UD_SEND_ONLY:
//       NS_LOG_INFO ("At time " << Simulator::Now ().As (Time::S) << " RoCEv2 server "
//                               << header.GetDestination () << " received " << packet->GetSize ()
//                               << " bytes from " << header.GetSource () << " with a "
//                               << rocev2Header);
//       break;
//     case RoCEv2Header::Opcode::CNP:
//       break;
//     }
// }

// static
Ptr<Packet>
RoCEv2L4Protocol::GenerateCNP(uint32_t srcQP, uint32_t dstQP)
{
    RoCEv2Header header{};
    header.SetOpcode(RoCEv2Header::Opcode::CNP);
    header.SetDestQP(dstQP);
    header.SetSrcQP(srcQP);
    Ptr<Packet> packet = Create<Packet>(16); // 16 bytes reserved
    packet->AddHeader(header);
    SocketIpTosTag ipTosTag;
    // The tos is 22, which will be interpreted as 6 (2nd-high priority) by Socket::IpTos2Priority
    uint8_t tos = 30; // 111 10
    ipTosTag.SetTos(tos); // high priority
    packet->AddPacketTag(ipTosTag);
    return packet;
}

// static
Ptr<Packet>
RoCEv2L4Protocol::GenerateACK(uint32_t srcQP,
                              uint32_t dstQP,
                              uint32_t expectedPSN,
                              const uint32_t payloadSize)
{
    RoCEv2Header rocev2Header{};
    rocev2Header.SetOpcode(RoCEv2Header::Opcode::RC_ACK);
    rocev2Header.SetDestQP(dstQP);
    rocev2Header.SetSrcQP(srcQP);
    rocev2Header.SetPSN(expectedPSN);
    AETHeader aeth;
    aeth.SetSyndromeType(AETHeader::SyndromeType::FC_DISABLED); // TODO: support flow control
    Ptr<Packet> packet = Create<Packet>(payloadSize);
    packet->AddHeader(aeth);
    packet->AddHeader(rocev2Header);
    return packet;
}

// static
Ptr<Packet>
RoCEv2L4Protocol::GenerateNACK(uint32_t srcQP,
                               uint32_t dstQP,
                               uint32_t expectedPSN,
                               const uint32_t payloadSize)
{
    RoCEv2Header rocev2Header{};
    rocev2Header.SetOpcode(RoCEv2Header::Opcode::RC_ACK);
    rocev2Header.SetDestQP(dstQP);
    rocev2Header.SetSrcQP(srcQP);
    rocev2Header.SetPSN(expectedPSN);
    AETHeader aeth;
    aeth.SetSyndromeType(AETHeader::SyndromeType::NACK);
    Ptr<Packet> packet = Create<Packet>(payloadSize);
    packet->AddHeader(aeth);
    packet->AddHeader(rocev2Header);
    return packet;
}

// static
bool
RoCEv2L4Protocol::IsCNP(Ptr<Packet> packet)
{
    RoCEv2Header rocev2Header;
    packet->PeekHeader(rocev2Header);
    return rocev2Header.GetOpcode() == RoCEv2Header::Opcode::CNP;
}

bool
RoCEv2L4Protocol::CheckCouldSend(uint32_t portIdx, uint32_t priority) const
{
    // Get device from port index
    Ptr<Node> node = this->GetObject<Node>();
    Ptr<NetDevice> netDevice = node->GetDevice(portIdx);

    Ptr<DcbNetDevice> dcbDev = DynamicCast<DcbNetDevice>(netDevice);
    if (dcbDev == nullptr)
    {
        return true;
    }

    Ptr<PausableQueueDisc> qdisc = DynamicCast<PausableQueueDisc>(dcbDev->GetQueueDisc());
    if (qdisc == nullptr)
    {
        return true;
    }

    QueueSize qsize = qdisc->GetInnerQueueSize(priority);

    return qsize.GetValue() == 0;
}

void
RoCEv2L4Protocol::RegisterSendPendingDataCallback(uint32_t portIdx,
                                                  uint32_t priority,
                                                  uint32_t innerPrio,
                                                  Callback<void> cb)
{
    // Add the cb to the m_sendCbQueue
    m_sendCbQueue[portIdx][priority][innerPrio].push_back(cb);
}

void 
RoCEv2L4Protocol::NotifyCouldSend(uint32_t portIdx, uint32_t priority)
{
    // Find the corresponding InnerPrioritySendCbQueue
    InnerPrioritySendCbQueue& innerPrioritySendCbQueue = m_sendCbQueue[portIdx][priority];

    // Find the first InnerPrioritySendCbQueue that is not empty
    for (auto& [innerPrio, sendCbQueue] : innerPrioritySendCbQueue)
    {
        if (!sendCbQueue.empty())
        {
            // Call the callback function
            sendCbQueue.front()();
            // Remove the callback function
            sendCbQueue.pop_front();
            return;
        }
    }
}

} // namespace ns3
