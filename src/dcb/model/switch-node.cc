#include "switch-node.h"

#include "ns3/dcb-traffic-control.h"
#include "ns3/internet-module.h"
#include "ns3/ipv4-global-routing.h"
#include "ns3/point-to-point-net-device.h"
#include "ns3/traffic-control-layer.h"
#include "ns3/random-variable-stream.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("SwitchNode");

NS_OBJECT_ENSURE_REGISTERED(SwitchNode);

// Define the static member variable
uint64_t SwitchNode::m_randStream = 0;

TypeId
SwitchNode::GetTypeId()
{
    static TypeId tid = TypeId("ns3::SwitchNode").
                        SetParent<Node>()
                        .AddConstructor<SwitchNode>()
                        .SetGroupName("Dcb")
                        .AddAttribute("RoutingMode",
                                      "The routing mode",
                                      EnumValue(RoutingMode::PER_PACKET_SYMMETRIC),
                                      MakeEnumAccessor(&SwitchNode::m_RoutingMode),
                                      MakeEnumChecker(
                                            RoutingMode::PER_PACKET, "PER_PACKET",
                                            RoutingMode::PER_FLOW_ECMP, "PER_FLOW_ECMP",
                                            RoutingMode::PER_PACKET_SYMMETRIC, "PER_PACKET_SYMMETRIC",
                                            RoutingMode::PER_FLOW_SYMMETRIC, "PER_FLOW_SYMMETRIC"
                                                      ));
    return tid;
}

SwitchNode::SwitchNode()
{
    NS_LOG_FUNCTION(this);
    m_rand = CreateObject<UniformRandomVariable>(); 
    m_rand->SetStream(SwitchNode::m_randStream++);
}

void
SwitchNode::DoInitialize()
{
    // for (uint32_t i = 0; i < GetNDevices(); i++)
    // {
    //     Ptr<NetDevice> dev = GetDevice(i);
    //     dev->SetReceiveCallback(MakeCallback(&SwitchNode::ReceiveFromDevice, this));
    // }

    // setup route table
    std::unordered_map<uint32_t, std::set<int>> routeTable;
    auto globalRouting = GetObject<GlobalRouter>()->GetRoutingProtocol();
    auto ipv4L3Proto = GetObject<Ipv4L3Protocol>();
    std::vector<int> iface2DevIdxMap;
    iface2DevIdxMap.resize(ipv4L3Proto->GetNInterfaces());
    for (uint32_t i = 0; i < GetNDevices(); i++)
    {
        auto dev = GetDevice(i);
        int ifaceNum = ipv4L3Proto->GetInterfaceForDevice(dev);
        iface2DevIdxMap[ifaceNum] = i;
    }
    for (uint32_t i = 0; i < globalRouting->GetNRoutes(); i++)
    {
        auto routeEntry = globalRouting->GetRoute(i);
        if (!routeEntry->IsHost())
        {
            continue;
        }
        uint32_t dstIp = routeEntry->GetDest().Get();
        int ifaceNum = routeEntry->GetInterface();
        auto devIdx = iface2DevIdxMap[ifaceNum];
        routeTable[dstIp].insert(devIdx);

        // NS_ASSERT_MSG(DynamicCast<PointToPointNetDevice>(GetDevice(devIdx)) != nullptr,
        //               "NetDevice must be PointToPointNetDevice or its subclass");
    }
    for (const auto& [dstIp, egressSet] : routeTable)
    {
        m_routeTable[dstIp] = std::vector<int>{egressSet.begin(), egressSet.end()};
        // NS_LOG_DEBUG("[Switch " << GetId() << "] ns3::GlobalRouting for " << Ipv4Address{dstIp}
        //                         << " = " << m_routeTable[dstIp]);
    }

    Node::DoInitialize();
}

uint32_t
SwitchNode::GetEgressDevIndex(Ptr<Packet> packet)
{
    Ipv4Header ipv4H;
    Ptr<Packet> p = packet->Copy();
    p->RemoveHeader(ipv4H);

    auto& egressNetDevs = m_routeTable[ipv4H.GetDestination().Get()];
    if (egressNetDevs.size() == 1)
    {
        return egressNetDevs[0];
    }

    HashBuf buf;
    uint32_t idx = 0;
    if (ipv4H.GetProtocol() == TcpL4Protocol::PROT_NUMBER)
    {
        TcpHeader tcpH;
        p->PeekHeader(tcpH);
        buf._srcIp = ipv4H.GetSource().Get();
        buf._dstIp = ipv4H.GetDestination().Get();
        buf._srcPort = tcpH.GetSourcePort();
        buf._dstPort = tcpH.GetDestinationPort();

        buf._srcPort += Simulator::GetContext();
        idx = Hash32(buf._b, HASH_BUF_SIZE) % egressNetDevs.size();
    }
    else if (ipv4H.GetProtocol() == UdpL4Protocol::PROT_NUMBER)
    {
        UdpHeader udpH;
        p->PeekHeader(udpH);
        if (udpH.GetSourcePort() == 4791) // RoCEv2L4Protocol::PROT_NUMBER
        {                                 // RoCEv2
            UdpRoCEv2Header udpRoCEheader;
            p->PeekHeader(udpRoCEheader);
            buf._srcIp = ipv4H.GetSource().Get();
            buf._dstIp = ipv4H.GetDestination().Get();
            buf._srcPort = udpRoCEheader.GetRoCE().GetSrcQP();
            buf._dstPort = udpRoCEheader.GetRoCE().GetDestQP();
        }
        else
        {
            buf._srcIp = ipv4H.GetSource().Get();
            buf._dstIp = ipv4H.GetDestination().Get();
            buf._srcPort = udpH.GetSourcePort();
            buf._dstPort = udpH.GetDestinationPort();
        }

        buf._srcPort += Simulator::GetContext();
        idx = Hash32(buf._b, HASH_BUF_SIZE) % egressNetDevs.size();
    }

    return egressNetDevs[idx];
}

uint32_t SwitchNode::GetEgressDevIndexRandom(Ptr<Packet> packet)
{
    Ipv4Header ipv4H;
    Ptr<Packet> p = packet->Copy();
    p->RemoveHeader(ipv4H);

    auto& egressNetDevs = m_routeTable[ipv4H.GetDestination().Get()];
    return egressNetDevs[m_rand->GetInteger(0, egressNetDevs.size() - 1)];
}
void
SwitchNode::SendIpv4Packet(Ptr<NetDevice> inDev, Ptr<Packet> packet)
{
    uint32_t devIdx;
    PathTag pathTag;
    switch (m_RoutingMode)
    {
    case PER_FLOW_ECMP:
        devIdx = GetEgressDevIndex(packet);
        break;
    case PER_PACKET:
        devIdx = GetEgressDevIndexRandom(packet);
        break;
    case PER_PACKET_SYMMETRIC:
    if(packet->PeekPacketTag(pathTag))
    {
        if(pathTag.forward){
            pathTag.AppendInterfaceIndex(inDev->GetIfIndex());
            packet->ReplacePacketTag(pathTag);
            devIdx=GetEgressDevIndexRandom(packet);
        }
        else{
            devIdx=pathTag.PopInterfaceIndex();
            packet->ReplacePacketTag(pathTag);
        }
    }
    else{
        pathTag.forward=true;
        pathTag.AppendInterfaceIndex(inDev->GetIfIndex());
        packet->AddPacketTag(pathTag);
        devIdx=GetEgressDevIndexRandom(packet);
    }//first hop
    break;
    case PER_FLOW_SYMMETRIC:
        if(packet->PeekPacketTag(pathTag))
        {

            if(pathTag.forward){
                pathTag.AppendInterfaceIndex(inDev->GetIfIndex());
                packet->ReplacePacketTag(pathTag);
                devIdx=GetEgressDevIndex(packet);
            }
            else{

                devIdx=pathTag.PopInterfaceIndex();
                packet->ReplacePacketTag(pathTag);
            }
        }
        else{
            
            pathTag.forward=true;
            pathTag.AppendInterfaceIndex(inDev->GetIfIndex());
            packet->AddPacketTag(pathTag);
            devIdx=GetEgressDevIndex(packet);
        }//first hop
        break;
    default:
        NS_FATAL_ERROR("SwitchNode::SendIpv4Packet: unknown routing mode " << m_RoutingMode);
        break;
    }
    auto dev = GetDevice(devIdx);
    
    Ipv4Header ipv4H;
    packet->RemoveHeader(ipv4H);
    ipv4H.SetTtl(ipv4H.GetTtl() - 1);

    // std::cout << GetId() << " " << ipv4H << std::endl;

    // DeviceIndexTag devTag;
    // devTag.SetIndex(devIdx);
    // packet->AddPacketTag(devTag);
    //
    // CoSTag cosTag;
    // uint8_t priority = DcbTrafficControl::PeekPriorityOfPacket(packet);
    // cosTag.SetCoS(priority);
    // packet->AddPacketTag(cosTag); // CoSTag is removed in EgressProcess

    Ptr<TrafficControlLayer> tc = GetObject<DcbTrafficControl>();
    if (tc == nullptr)
    {
        tc = GetObject<TrafficControlLayer>();
    }

    tc->Send(
        dev,
        Create<Ipv4QueueDiscItem>(packet, dev->GetAddress(), Ipv4L3Protocol::PROT_NUMBER, ipv4H));
}

bool
SwitchNode::ReceiveFromDevice(Ptr<NetDevice> device,
                              Ptr<const Packet> p,
                              uint16_t protocol,
                              const Address& from)
{
    if (protocol != Ipv4L3Protocol::PROT_NUMBER)
    {
        NS_LOG_ERROR("SwitchNode Recv Packet with non-ipv4 protol 0x" << std::hex << protocol
                                                                      << std::dec);
        return false;
    }
    ReceiveIpv4Packet(device, p);
    return true;
}

void
SwitchNode::ReceiveIpv4Packet(Ptr<NetDevice> inDev, Ptr<const Packet> packet)
{
    SendIpv4Packet(inDev,packet->Copy());
}

void
SwitchNode::ReceivePacketAfterTc(Ptr<NetDevice> dev,
                                 Ptr<const Packet> packet,
                                 uint16_t protocol,
                                 const Address& from,
                                 const Address& to,
                                 NetDevice::PacketType packetType)
{
    NS_LOG_FUNCTION(this << dev << protocol << from << to);
    SendIpv4Packet(dev,packet->Copy());
}

} // namespace ns3