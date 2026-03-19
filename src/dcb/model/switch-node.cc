#include "switch-node.h"

#include <iomanip>
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

// Define the static member variables
uint64_t SwitchNode::m_randStream = 0;
std::map<uint32_t, std::vector<std::pair<uint32_t, int>>> SwitchNode::s_multipathRoutes;

// Debug counters
static uint64_t g_sw_forwarded       = 0;  ///< packets forwarded via tc->Send
static uint64_t g_sw_noroute         = 0;  ///< packets routed to default port 1 (no exact route)
static uint64_t g_sw_recv_after_tc   = 0;  ///< ReceivePacketAfterTc calls
static uint64_t g_sw_recv_from_dev   = 0;  ///< ReceiveFromDevice calls
static uint32_t s_swRoutingTableSize = 0;  ///< total routing table entries (set during DoInit)

TypeId
SwitchNode::GetTypeId()
{
    static TypeId tid = TypeId("ns3::SwitchNode").
                        SetParent<Node>()
                        .AddConstructor<SwitchNode>()
                        .SetGroupName("Dcb")
                        .AddAttribute("RoutingMode",
                                      "The routing mode",
                                      EnumValue(RoutingMode::PER_FLOW_ECMP),
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
    // ── Prevent double-processing of transit packets ──────────────────────
    //
    // By default, Ipv4L3Protocol::AddInterface() registers BOTH:
    //   (a) TrafficControlLayer::Receive  on the Node
    //   (b) Ipv4L3Protocol::Receive       on the TrafficControlLayer
    //
    // And json-topology-helper.cc adds:
    //   (c) SwitchNode::ReceivePacketAfterTc  on the TrafficControlLayer
    //
    // When a packet arrives at a switch, the base TrafficControlLayer::Receive()
    // calls ALL matching handlers → both (b) and (c) → double processing.
    //
    // DcbTrafficControl (used by RoCEv2) avoids this by overriding Receive()
    // with reverse-iteration + break, so only (c) runs.
    //
    // For all switch nodes (NDP and RoCEv2 alike), we remove handler (b) from
    // the TC layer and keep only (c).  This is safe for RoCEv2 because
    // DcbTrafficControl::Receive() already skips (b) via its break logic;
    // removing it just eliminates dead code.
    //
    // This replaces the previous approach which bypassed the entire
    // TrafficControlLayer by overriding m_rxCallback on each device.
    // Now NDP follows the SAME standard ns-3 receive path as RoCEv2:
    //   DcbNetDevice::Receive()
    //     → Node::NonPromiscReceiveFromDevice()
    //       → TrafficControlLayer::Receive()
    //         → SwitchNode::ReceivePacketAfterTc()   [only handler]
    //           → SwitchNode::SendIpv4Packet()       [ECMP routing]
    //             → TrafficControlLayer::Send()
    //               → QueueDisc::Enqueue()
    //                 → DcbNetDevice::Send()
    // ──────────────────────────────────────────────────────────────────────
    {
        Ptr<TrafficControlLayer> tc = GetObject<TrafficControlLayer>();
        if (tc)
        {
            for (uint32_t i = 1; i < GetNDevices(); i++)   // skip loopback (dev 0)
            {
                Ptr<NetDevice> dev = GetDevice(i);

                // Ensure the Node layer always has an IPv4 handler on switch ports.
                // Some NDP runs never reached TrafficControlLayer::Receive on switches,
                // leaving transit packets stranded after DcbNetDevice::Receive().
                RegisterProtocolHandler(
                    MakeCallback(&SwitchNode::ReceivePacketAfterTc, this),
                    Ipv4L3Protocol::PROT_NUMBER,
                    dev);

                // Remove all IPv4 handlers on TC for this device
                // (this removes Ipv4L3Protocol::Receive AND any previously
                //  registered ReceivePacketAfterTc from json-topology-helper)
                tc->ClearProtocolHandlers(Ipv4L3Protocol::PROT_NUMBER, dev);

                // Re-register ONLY our custom forwarding handler
                tc->RegisterProtocolHandler(
                    MakeCallback(&SwitchNode::ReceivePacketAfterTc, this),
                    Ipv4L3Protocol::PROT_NUMBER,
                    dev);
            }
            NS_LOG_INFO("SwitchNode " << GetId()
                        << ": Replaced TC handlers with ReceivePacketAfterTc on "
                        << (GetNDevices() - 1) << " devices");
        }
    }

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
    s_swRoutingTableSize += m_routeTable.size();

    // ── Apply pre-registered multipath routes ─────────────────────────────
    // These deterministic routes override any ECMP entries from GlobalRouter.
    // They are registered by NdpMultipathHelper::ConfigureFatTreeRouting()
    // before Simulator::Run(), so they are available here.
    {
        auto mIt = s_multipathRoutes.find(GetId());
        if (mIt != s_multipathRoutes.end())
        {
            for (auto& [ip, devIdx] : mIt->second)
            {
                m_routeTable[ip] = {devIdx};  // single deterministic next-hop
            }
            NS_LOG_INFO("SwitchNode " << GetId()
                        << ": Injected " << mIt->second.size()
                        << " multipath routes");
        }
    }

    Node::DoInitialize();
}

void
SwitchNode::AddMultipathRoute(uint32_t switchId, uint32_t ip, int devIdx)
{
    s_multipathRoutes[switchId].emplace_back(ip, devIdx);
}

void
SwitchNode::ClearMultipathRoutes()
{
    s_multipathRoutes.clear();
}

uint32_t
SwitchNode::GetEgressDevIndex(Ptr<Packet> packet)
{
    Ipv4Header ipv4H;
    Ptr<Packet> p = packet->Copy();
    p->RemoveHeader(ipv4H);

    Ipv4Address destAddr = ipv4H.GetDestination();
    auto it = m_routeTable.find(destAddr.Get());
    
    // If exact destination not found, try subnet matching
    if (it == m_routeTable.end() || it->second.empty())
    {
        uint32_t destIp = destAddr.Get();
        
        // Try /16 subnet match
        uint32_t subnet16 = destIp & 0xFFFF0000;
        it = m_routeTable.find(subnet16);
        
        if (it == m_routeTable.end() || it->second.empty())
        {
            // Try /24 subnet match
            uint32_t subnet24 = destIp & 0xFFFFFF00;
            it = m_routeTable.find(subnet24);
            
            if (it == m_routeTable.end() || it->second.empty())
            {
                g_sw_noroute++;
                return 1;  // Default interface
            }
        }
    }
    
    auto& egressNetDevs = it->second;
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

    Ipv4Address destAddr = ipv4H.GetDestination();
    auto it = m_routeTable.find(destAddr.Get());
    
    // If exact destination not found, try subnet matching
    if (it == m_routeTable.end() || it->second.empty())
    {
        // Try to find a route by matching subnet (e.g., 10.0.x.x matches 10.0.0.0/16)
        uint32_t destIp = destAddr.Get();
        
        // Try /16 subnet match (mask: 255.255.0.0)
        uint32_t subnet16 = destIp & 0xFFFF0000;
        it = m_routeTable.find(subnet16);
        
        if (it == m_routeTable.end() || it->second.empty())
        {
            // Try /24 subnet match (mask: 255.255.255.0)
            uint32_t subnet24 = destIp & 0xFFFFFF00;
            it = m_routeTable.find(subnet24);
            
            if (it == m_routeTable.end() || it->second.empty())
            {
                // No route found, use default route (interface 1)
                g_sw_noroute++;
                return 1;
            }
        }
    }
    
    auto& egressNetDevs = it->second;
    if (egressNetDevs.empty())
    {
        // Fallback to interface 1 if route table entry is empty
        g_sw_noroute++;
        return 1;
    }
    
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

    g_sw_forwarded++;
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
    g_sw_recv_from_dev++;
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
    g_sw_recv_after_tc++;
    SendIpv4Packet(dev,packet->Copy());
}

void
PrintSwitchNodeStats()
{
    std::cout << "╔══════════════════════════════════════════════════╗\n"
              << "║        SwitchNode Routing Statistics             ║\n"
              << "╠══════════════════════════════════════════════════╣\n"
              << "║  ReceivePacketAfterTc calls:   " << std::setw(8) << g_sw_recv_after_tc << "  ║\n"
              << "║  ReceiveFromDevice calls:      " << std::setw(8) << g_sw_recv_from_dev << "  ║\n"
              << "║  Total packets forwarded (tc): " << std::setw(8) << g_sw_forwarded     << "  ║\n"
              << "║  Default-route fallbacks:      " << std::setw(8) << g_sw_noroute       << "  ║\n"
              << "║  Switch routing table entries: " << std::setw(8) << s_swRoutingTableSize << "  ║\n"
              << "╚══════════════════════════════════════════════════╝\n";
}

} // namespace ns3
