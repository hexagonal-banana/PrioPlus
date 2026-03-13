#include "ndp-l4-protocol.h"

#include "ns3/ndp-header.h"
#include "ndp-socket.h"
#include "ns3/simulator.h"
#include "ns3/data-rate.h"
#include "ns3/string.h"
#include "ns3/uinteger.h"

#include <algorithm>
#include <fstream>
#include <chrono>
#include <iomanip>

#include "ns3/ipv4.h"
#include "ns3/queue-disc.h"
#include "ns3/ipv4-interface.h"
#include "ns3/ipv4-l3-protocol.h"
#include "ns3/ipv4-route.h"
#include "ns3/ipv4-routing-protocol.h"
#include "ns3/ipv4-queue-disc-item.h"
#include "ns3/ipv6-interface.h"
#include "ns3/log.h"
#include "ns3/net-device.h"
#include "ns3/node.h"
#include "ns3/packet.h"
#include "ns3/traffic-control-layer.h"

namespace ns3
{
 
 NS_LOG_COMPONENT_DEFINE("NdpL4Protocol");
 
 NS_OBJECT_ENSURE_REGISTERED(NdpL4Protocol);
 
 const uint8_t NdpL4Protocol::PROT_NUMBER = 253; // Custom protocol number
 
 TypeId
NdpL4Protocol::GetTypeId()
{
    static TypeId tid = TypeId("ns3::NdpL4Protocol")
                            .SetParent<IpL4Protocol>()
                            .SetGroupName("Dcb")
                            .AddConstructor<NdpL4Protocol>()
                            .AddAttribute("PullLinkRate",
                                          "Egress link rate used for PULL pacing. "
                                          "NdpApplicationHelper auto-sets this from the node's "
                                          "DcbNetDevice at install time, so manual configuration "
                                          "is normally unnecessary.  This attribute serves as a "
                                          "fallback only when no DcbNetDevice is found.",
                                          DataRateValue(DataRate("100Gbps")),
                                          MakeDataRateAccessor(&NdpL4Protocol::m_pullLinkRate),
                                          MakeDataRateChecker())
                            .AddAttribute("PullMtu",
                                          "Effective MTU (bytes) used to compute the per-pull pacing "
                                          "interval: interval = PullMtu × 8 / PullLinkRate. "
                                          "Must be set ABOVE the real data MTU (1500 B) to leave "
                                          "headroom for control-packet overhead on the bottleneck link. "
                                          "With strict-priority scheduling, trim headers (~80 B) share "
                                          "the same egress link as data.  Setting PullMtu = MTU + "
                                          "ctrl_hdr_size = 1500 + 80 = 1580 slows PULL pacing by ~5 %, "
                                          "giving the switch enough bandwidth margin to forward trim "
                                          "headers without causing a positive-feedback trim spiral. "
                                          "Using the raw MTU (1500 B) paces data at exactly the link "
                                          "rate, leaving ZERO headroom → any perturbation (e.g. initial "
                                          "window burst) triggers a sustained ~90 % trim rate.",
                                          UintegerValue(1580),
                                          MakeUintegerAccessor(&NdpL4Protocol::m_pullMtu),
                                          MakeUintegerChecker<uint32_t>());
    return tid;
}

// ── Global pull pipeline counters (filled by ndp-l4-protocol.cc) ─────────────
static uint64_t g_pull_enqueued   = 0;   ///< EnqueuePull() calls (receiver side)
static uint64_t g_pull_sent       = 0;   ///< SendOnePull() calls (actual PULL pkts sent)
static uint64_t g_pull_rx_total   = 0;   ///< PULL pkts arriving at Receive() (any node)
static uint64_t g_pull_rx_nosock  = 0;   ///< PULL pkts arriving but no socket found
static uint64_t g_send_calls      = 0;   ///< NdpL4Protocol::Send() total calls
static uint64_t g_send_no_route   = 0;   ///< NdpL4Protocol::Send() no route found

// ── Forward declarations for DcbNetDevice NIC-level counters ────────────────
extern uint64_t g_nic_ndp_rx;    ///< NDP pkts received at any NIC (from dcb-net-device.cc)
extern uint64_t g_nic_ndp_tx;    ///< NDP pkts transmitted at any NIC (from dcb-net-device.cc)
extern uint64_t g_nic_ndp_drop;  ///< NDP pkts dropped at DcbNetDevice m_queue (from dcb-net-device.cc)

// ── NDP SIMULATION CORRECTNESS REPORT (temporarily disabled) ─────────────────
// Forward declarations for stats-print functions defined in other .cc files.
// void PrintNdpSwitchStats();
// void PrintNdpSocketStats();
// void PrintNdpHostQueueStats();
// void PrintSwitchNodeStats();
// void PrintLinkUtilizationStats();
//
// static void
// PrintNdpAllStats()
// {
//     std::cout << "\n"
//               << "══════════════════════════════════════════════════════\n"
//               << "  NDP SIMULATION CORRECTNESS REPORT\n"
//               << "══════════════════════════════════════════════════════\n";
//     PrintNdpHostQueueStats();
//     PrintNdpSwitchStats();
//     PrintSwitchNodeStats();
//     std::cout << "╔══════════════════════════════════════════════════╗\n"
//               << "║          NDP Pull Pipeline Statistics            ║\n"
//               << "╠══════════════════════════════════════════════════╣\n"
//               << "║  L4 Send() total calls          : " << std::setw(8) << g_send_calls     << "              ║\n"
//               << "║  L4 Send() no route (dropped)   : " << std::setw(8) << g_send_no_route  << "              ║\n"
//               << "║  EnqueuePull() calls (rx side)  : " << std::setw(8) << g_pull_enqueued  << "              ║\n"
//               << "║  SendOnePull() calls (tx pkts)  : " << std::setw(8) << g_pull_sent      << "              ║\n"
//               << "║  NDP pkts sent by any NIC (tx)  : " << std::setw(8) << g_nic_ndp_tx     << "              ║\n"
//               << "║  NDP pkts arriving at any NIC   : " << std::setw(8) << g_nic_ndp_rx     << "              ║\n"
//               << "║  (Note: L4 sends=" << std::setw(6) << g_send_calls << " NIC tx=" << std::setw(6) << g_nic_ndp_tx << " diff=" << static_cast<int64_t>(g_send_calls) - static_cast<int64_t>(g_nic_ndp_tx) << ") ║\n"
//               << "║  PULL pkts arriving at Receive(): " << std::setw(8) << g_pull_rx_total  << "              ║\n"
//               << "║  PULL pkts with no socket found : " << std::setw(8) << g_pull_rx_nosock << "              ║\n"
//               << "║  NIC m_queue drops (DcbNetDev)  : " << std::setw(8) << g_nic_ndp_drop   << "              ║\n"
//               << "╚══════════════════════════════════════════════════╝\n";
//     PrintNdpSocketStats();
//     PrintLinkUtilizationStats();
//     std::cout << "══════════════════════════════════════════════════════\n\n";
// }

NdpL4Protocol::NdpL4Protocol()
    : m_nextPort(49152), // Start of dynamic port range
      m_pullLinkRate(DataRate("100Gbps")),  // fallback; NdpApplicationHelper overwrites from DcbNetDevice
      m_pullMtu(1580)  // 1500B data MTU + 80B ctrl-header headroom → 5% slower PULL pacing
{
    NS_LOG_FUNCTION(this);

    // NDP SIMULATION CORRECTNESS REPORT (temporarily disabled)
    // static bool s_statsScheduled = false;
    // if (!s_statsScheduled)
    // {
    //     s_statsScheduled = true;
    //     Simulator::ScheduleDestroy(&PrintNdpAllStats);
    // }
}
 
NdpL4Protocol::~NdpL4Protocol()
{
    NS_LOG_FUNCTION(this);
}

void
NdpL4Protocol::NotifyNewAggregate()
{
    NS_LOG_FUNCTION(this);
    
    // This method is called when the protocol is aggregated to a node
    Ptr<Node> node = this->GetObject<Node>();
    Ptr<Ipv4> ipv4 = this->GetObject<Ipv4>();
    
    if (!m_node)
    {
        if (node && ipv4)
        {
            m_node = node;
            NS_LOG_DEBUG("NdpL4Protocol aggregated to node " << node->GetId());
        }
    }
    
    // Set up down target callbacks (standard ns-3 architecture)
    // This allows NDP to use the standard Ipv4::Send path
    if (ipv4 && m_downTarget.IsNull())
    {
        this->SetDownTarget(MakeCallback(&Ipv4::Send, ipv4));
        NS_LOG_DEBUG("NdpL4Protocol: Set down target to Ipv4::Send");
    }
    
    // Call parent class
    IpL4Protocol::NotifyNewAggregate();
}

void
NdpL4Protocol::DoDispose()
{
    NS_LOG_FUNCTION(this);

    // Cancel global pull pacing timer first
    if (m_globalPullEvent.IsRunning())
    {
        m_globalPullEvent.Cancel();
    }
    m_globalPullQueue.clear();
    m_connSockets.clear();
    m_sockets.clear();
    m_socketList.clear();
    m_node = nullptr;
    m_downTarget.Nullify();
    m_downTarget6.Nullify();

    IpL4Protocol::DoDispose();
}

void
NdpL4Protocol::SetNode(Ptr<Node> node)
{
     NS_LOG_FUNCTION(this << node);
     m_node = node;
 }
 
Ptr<Node>
NdpL4Protocol::GetNode() const
{
    return m_node;
}
 
 int
 NdpL4Protocol::GetProtocolNumber() const
 {
     return PROT_NUMBER;
 }
 
enum IpL4Protocol::RxStatus
NdpL4Protocol::Receive(Ptr<Packet> packet,
                       const Ipv4Header& header,
                       Ptr<Ipv4Interface> incomingInterface)
{
    NS_LOG_FUNCTION(this << packet << header);
    
    // std::cout << "      🔺 [L4-RX] Node=" << m_node->GetId()
    //           << " " << header.GetSource() << " → " << header.GetDestination()
    //           << " Size=" << packet->GetSize() << "B"
    //           << std::endl;
    
    // CRITICAL FIX: Check if packet size is sufficient for NdpHeader
    if (packet->GetSize() < 22)
    {
        NS_LOG_WARN("Packet too small for NdpHeader: " << packet->GetSize() << " bytes");
        return IpL4Protocol::RX_ENDPOINT_UNREACH;
    }
    
    NdpHeader ndpHeader;
    packet->PeekHeader(ndpHeader);
    
    // Extract port information from connection ID.
    // ✅ New encoding (after Bug-1 fix): (srcIP << 32) | (srcPort << 16) | dstPort
    //   Old (broken) encoding was: (srcPort << 16) | dstPort — caused connId collisions
    //   when multiple nodes independently allocated the same ephemeral port.
    // Port extraction is bit-compatible with the old scheme:
    //   senderLocalPort  = (connId >> 16) & 0xFFFF  ← bits[31:16] = srcPort
    //   senderRemotePort = connId & 0xFFFF           ← bits[15:0]  = dstPort
    uint64_t connId = ndpHeader.GetConnectionId();
    // std::cout << "🔍 [Debug] connId=0x" << std::hex << connId << std::dec << std::endl;
    
    uint16_t senderLocalPort = (connId >> 16) & 0xFFFF;   // Sender's local port (发送方本地端口)
    uint16_t senderRemotePort = connId & 0xFFFF;          // Sender's remote port (发送方的远程端口)
    
    // std::cout << "🔍 [Debug] senderLocalPort=" << senderLocalPort 
    //           << " senderRemotePort=" << senderRemotePort << std::endl;
    
    NS_LOG_DEBUG("Received NDP packet: " << header.GetSource() << " -> "
                                         << header.GetDestination()
                                         << " size=" << packet->GetSize()
                                         << " connId=0x" << std::hex << connId << std::dec);

    // Track PULL packet arrivals for debugging
    if (ndpHeader.IsPull()) { g_pull_rx_total++; }

    // ── Step 1: Route by connId (per-flow accepted socket) ──────────────────
    // DATA / TRIM / RTS / ACK going to an already-established receiver socket.
    Ptr<NdpSocket> socket = FindSocketByConnId(connId);
    if (socket)
    {
        socket->ForwardUp(packet, header, senderLocalPort, incomingInterface);
        return IpL4Protocol::RX_OK;
    }

    // ── Step 2: SYN → create accepted socket on receiver side ────────────────
    if (ndpHeader.IsSyn())
    {
        // Look for a LISTEN socket on the receiver's port
        Ptr<NdpSocket> listenSocket = FindSocketByPort(senderRemotePort);
        if (listenSocket && listenSocket->GetState() == NdpSocket::LISTEN)
        {
            uint32_t firstSeq = ndpHeader.GetSequence() - ndpHeader.GetSeqOffset();
            Ptr<NdpSocket> accepted = CreateAcceptedSocket(header.GetDestination(),
                                                           senderRemotePort,
                                                           header.GetSource(),
                                                           senderLocalPort,
                                                           connId,
                                                           firstSeq);
            // Forward the SYN packet (and any piggybacked DATA) to the new socket
            accepted->ForwardUp(packet, header, senderLocalPort, incomingInterface);
            return IpL4Protocol::RX_OK;
        }
    }

    // ── Step 3: Sender-side packets (ACK / NACK / PULL / RTS response) ───────
    // These carry the sender's local port in connId[31:16].
    socket = FindSocketByPort(senderLocalPort);
    if (socket)
    {
        socket->ForwardUp(packet, header, senderLocalPort, incomingInterface);
        return IpL4Protocol::RX_OK;
    }

    // ── Step 4: Fallback – look up by receiver port (legacy / edge cases) ────
    socket = FindSocketByPort(senderRemotePort);
    if (socket)
    {
        socket->ForwardUp(packet, header, senderLocalPort, incomingInterface);
        return IpL4Protocol::RX_OK;
    }

    NS_LOG_DEBUG("No socket found for connectionId 0x" << std::hex << connId << std::dec);
    if (ndpHeader.IsPull()) { g_pull_rx_nosock++; }
    return IpL4Protocol::RX_ENDPOINT_UNREACH;
}
 
 enum IpL4Protocol::RxStatus
 NdpL4Protocol::Receive(Ptr<Packet> p,
                        const Ipv6Header& header,
                        Ptr<Ipv6Interface> incomingInterface)
 {
     NS_LOG_FUNCTION(this << p << header);
     // IPv6 not supported
     return IpL4Protocol::RX_ENDPOINT_UNREACH;
 }
 
//  void
//  NdpL4Protocol::ReceiveIcmp(Ipv4Address icmpSource,
//                             uint8_t icmpTtl,
//                             uint8_t icmpType,
//                             uint8_t icmpCode,
//                             uint32_t icmpInfo,
//                             Ipv4Address payloadSource,
//                             Ipv4Address payloadDestination,
//                             const uint8_t payload[8])
//  {
//      NS_LOG_FUNCTION(this << icmpSource << (uint32_t)icmpTtl << (uint32_t)icmpType
//                           << (uint32_t)icmpCode << icmpInfo << payloadSource << payloadDestination);
//      // ICMP handling not implemented for NDP
//  }
 
//  void
//  NdpL4Protocol::ReceiveIcmp(Ipv6Address icmpSource,
//                             uint8_t icmpTtl,
//                             uint8_t icmpType,
//                             uint8_t icmpCode,
//                             uint32_t icmpInfo,
//                             Ipv6Address payloadSource,
//                             Ipv6Address payloadDestination,
//                             const uint8_t payload[8])
//  {
//      NS_LOG_FUNCTION(this << icmpSource << (uint32_t)icmpTtl << (uint32_t)icmpType
//                           << (uint32_t)icmpCode << icmpInfo << payloadSource << payloadDestination);
//      // IPv6 not supported
//  }
 
 void
 NdpL4Protocol::SetDownTarget(IpL4Protocol::DownTargetCallback cb)
 {
     NS_LOG_FUNCTION(this);
     m_downTarget = cb;
 }
 
 void
 NdpL4Protocol::SetDownTarget6(IpL4Protocol::DownTargetCallback6 cb)
 {
     NS_LOG_FUNCTION(this);
     m_downTarget6 = cb;
 }
 
 IpL4Protocol::DownTargetCallback
 NdpL4Protocol::GetDownTarget() const
 {
     return m_downTarget;
 }
 
 IpL4Protocol::DownTargetCallback6
 NdpL4Protocol::GetDownTarget6() const
 {
     return m_downTarget6;
 }
 
 Ptr<NdpSocket>
 NdpL4Protocol::CreateSocket()
 {
     NS_LOG_FUNCTION(this);
     
     Ptr<NdpSocket> socket = CreateObject<NdpSocket>();
     socket->SetNdp(this);
     RegisterSocket(socket);
     
     return socket;
 }
 
 uint16_t
 NdpL4Protocol::AllocatePort()
 {
     NS_LOG_FUNCTION(this);
     
     // Find next available port
     while (m_sockets.find(m_nextPort) != m_sockets.end())
     {
         m_nextPort++;
         if (m_nextPort == 0)
         {
             m_nextPort = 49152; // Wrap around to start of dynamic range
         }
     }
     
     uint16_t port = m_nextPort++;
     NS_LOG_DEBUG("Allocated port " << port);
     return port;
 }
 
 void
 NdpL4Protocol::DeAllocatePort(uint16_t port)
 {
     NS_LOG_FUNCTION(this << port);
     
     auto it = m_sockets.find(port);
     if (it != m_sockets.end())
     {
         m_sockets.erase(it);
         NS_LOG_DEBUG("Deallocated port " << port);
     }
 }
 
void
NdpL4Protocol::Send(Ptr<Packet> packet,
                    Ipv4Address saddr,
                    Ipv4Address daddr,
                    uint16_t sport,
                    uint16_t dport)
{
    NS_LOG_FUNCTION(this << packet << saddr << daddr << sport << dport);
    
    // std::cout << "      🔻 [L4-TX] Node=" << m_node->GetId()
    //           << " " << saddr << ":" << sport
    //           << " → " << daddr << ":" << dport
    //           << " Size=" << packet->GetSize() << "B"
    //           << std::endl;
    
    g_send_calls++;

    // Check if down target is set
    if (m_downTarget.IsNull())
    {
        NS_LOG_WARN("NdpL4Protocol::Send: down target callback is not set");
        return;
    }
    
    // NDP Header is already added by NdpSocket, no need to add it here
    // Port information is encoded in the NDP Header's connectionId field
    
    // Find route to destination
    Ptr<Ipv4> ipv4 = m_node->GetObject<Ipv4>();
    if (!ipv4)
    {
        NS_LOG_WARN("Cannot send packet: IPv4 not found");
        return;
    }
    
    Ptr<Ipv4RoutingProtocol> routing = ipv4->GetRoutingProtocol();
    if (!routing)
    {
        NS_LOG_WARN("Cannot send packet: routing protocol not found");
        return;
    }
    
    // Build a temporary IPv4 header for routing
    Ipv4Header ipHeader;
    ipHeader.SetSource(saddr);
    ipHeader.SetDestination(daddr);
    ipHeader.SetProtocol(PROT_NUMBER);
    ipHeader.SetPayloadSize(packet->GetSize());
    ipHeader.SetTtl(64);
    
    Ptr<NetDevice> oif = nullptr;
    Socket::SocketErrno sockerr;
    Ptr<Ipv4Route> route = routing->RouteOutput(packet, ipHeader, oif, sockerr);
    
    if (!route)
    {
        g_send_no_route++;
        NS_LOG_WARN("Cannot send packet: no route to " << daddr);
        return;
    }
    
    NS_LOG_DEBUG("NDP Send via standard path: " << saddr << ":" << sport << " -> " 
                 << daddr << ":" << dport << " size=" << packet->GetSize());
    
    // std::cout << "📤 [Layer3-L4Protocol] Calling m_downTarget (Ipv4::Send)" << std::endl;
    
    // Use standard down target callback (Ipv4::Send)
    // This will automatically go through TrafficControlLayer and NdpSwitchQueue
    m_downTarget(packet, saddr, daddr, PROT_NUMBER, route);
    
    // std::cout << "✅ [Layer3-L4Protocol] m_downTarget returned successfully" << std::endl;
    NS_LOG_DEBUG("NDP packet sent successfully via m_downTarget");
}
 
void
NdpL4Protocol::RegisterSocket(Ptr<NdpSocket> socket)
{
    NS_LOG_FUNCTION(this << socket);
    m_socketList.push_back(socket);
}

void
NdpL4Protocol::RegisterSocketWithPort(Ptr<NdpSocket> socket, uint16_t port)
{
    NS_LOG_FUNCTION(this << socket << port);
    m_sockets[port] = socket;
    NS_LOG_DEBUG("Registered socket with port " << port);
}
 
void
NdpL4Protocol::UnregisterSocket(Ptr<NdpSocket> socket)
{
    NS_LOG_FUNCTION(this << socket);
     
    // Remove from list
    for (auto it = m_socketList.begin(); it != m_socketList.end(); ++it)
    {
        if (*it == socket)
        {
            m_socketList.erase(it);
            break;
        }
    }
     
    // Remove from port map
    for (auto it = m_sockets.begin(); it != m_sockets.end(); ++it)
    {
        if (it->second == socket)
        {
            m_sockets.erase(it);
            break;
        }
    }
}
 
Ptr<NdpSocket>
NdpL4Protocol::FindSocketByPort(uint16_t port)
{
    auto it = m_sockets.find(port);
    if (it != m_sockets.end())
    {
        return it->second;
    }
    return nullptr;
}

Ptr<NdpSocket>
NdpL4Protocol::FindSocketByConnId(uint64_t connId)
{
    auto it = m_connSockets.find(connId);
    if (it != m_connSockets.end())
    {
        return it->second;
    }
    return nullptr;
}

void
NdpL4Protocol::UnregisterConnId(uint64_t connId)
{
    NS_LOG_FUNCTION(this << connId);
    m_connSockets.erase(connId);
}

// ── Per-flow accepted socket creation ────────────────────────────────────────
Ptr<NdpSocket>
NdpL4Protocol::CreateAcceptedSocket(Ipv4Address localAddr,
                                     uint16_t    localPort,
                                     Ipv4Address remoteAddr,
                                     uint16_t    remotePort,
                                     uint64_t    connId,
                                     uint32_t    firstSeq)
{
    NS_LOG_FUNCTION(this << localAddr << localPort << remoteAddr << remotePort
                         << connId << firstSeq);

    Ptr<NdpSocket> socket = CreateObject<NdpSocket>();
    socket->SetNdp(this); // also sets m_node via ndp->GetNode()

    socket->SetupAsReceiver(localAddr, localPort, remoteAddr, remotePort,
                            connId, firstSeq);

    // Register in connSockets for per-flow routing
    m_connSockets[connId] = socket;
    // Keep in socketList for housekeeping
    RegisterSocket(socket);

    // ── Notify the LISTEN socket so the application can attach callbacks ──────
    // This mirrors the standard Accept() model: the application's
    // "new connection created" callback receives the accepted socket,
    // which lets it call SetRecvCallback() on that socket.
    Ptr<NdpSocket> listenSocket = FindSocketByPort(localPort);
    if (listenSocket && listenSocket->GetState() == NdpSocket::LISTEN)
    {
        Address fromAddr = InetSocketAddress(remoteAddr, remotePort);
        listenSocket->NotifyAccepted(socket, fromAddr);  // public wrapper around protected NotifyNewConnectionCreated
    }

    NS_LOG_INFO("Created accepted socket for connId=0x" << std::hex << connId
                << std::dec << " remote=" << remoteAddr << ":" << remotePort);
    return socket;
}

// ── Global pull queue ─────────────────────────────────────────────────────────
void
NdpL4Protocol::EnqueuePull(Ptr<NdpSocket> socket)
{
    NS_LOG_FUNCTION(this << socket);
    g_pull_enqueued++;

    // ✅ Fair round-robin: each socket appears at most once in m_globalPullQueue.
    // Increment the per-socket credit counter.  If this is the first credit for
    // this socket (counter was 0 / not present), append it to the round-robin list.
    auto& credits = m_pullCredits[socket];   // inserts 0 if absent
    if (credits == 0)
    {
    m_globalPullQueue.push_back(socket);
    }
    credits++;

    // ── Start / restart the pacing timer ─────────────────────────────────────
    //
    // CRITICAL FIX: Previously this called SendGlobalPulls() SYNCHRONOUSLY,
    // which consumed the just-added credit immediately (0 ns delay).  Because
    // each EnqueuePull() added exactly 1 credit and SendGlobalPulls() consumed
    // it in the same event, the m_pullMtu-based pacing interval was NEVER used.
    // PULLs were effectively sent at the packet-arrival rate (~156 M/s during
    // trim bursts) instead of the intended ~8 M/s, saturating the bottleneck
    // link and causing a 90% trim spiral.
    //
    // FIX: Schedule SendGlobalPulls() with the pacing interval so that PULLs
    // are rate-limited to: 1 PULL every (m_pullMtu × 8 / m_pullLinkRate) ns.
    // ─────────────────────────────────────────────────────────────────────────
    if (!m_globalPullEvent.IsRunning())
    {
        double bitsPerPacket = static_cast<double>(m_pullMtu) * 8.0;
        double bitsPerSecond = m_pullLinkRate.GetBitRate();
        Time   interval      = Seconds(bitsPerPacket / bitsPerSecond);
        m_globalPullEvent    = Simulator::Schedule(interval,
                                                   &NdpL4Protocol::SendGlobalPulls,
                                                   this);
    }
}

void
NdpL4Protocol::CancelPullsForSocket(Ptr<NdpSocket> socket)
{
    NS_LOG_FUNCTION(this << socket);

    // Remove per-socket credit counter
    m_pullCredits.erase(socket);

    // Remove the single entry (if any) from the round-robin active list
    auto it = std::find(m_globalPullQueue.begin(), m_globalPullQueue.end(), socket);
    if (it != m_globalPullQueue.end())
    {
        m_globalPullQueue.erase(it);
    }
}

void
NdpL4Protocol::SendGlobalPulls()
{
    NS_LOG_FUNCTION(this);

    // ── Fair round-robin scheduling ──────────────────────────────────────────
    // Each active socket appears exactly once in m_globalPullQueue.
    // We pop the front socket, consume 1 credit, and:
    //   • if credits remain → push it to the BACK (round-robin)
    //   • if credits exhausted → remove from credit map (not re-added)
    // This guarantees max-min fair bandwidth across all active flows.
    // ─────────────────────────────────────────────────────────────────────────

    // Skip any stale queue entries (socket disposed or credits already zeroed)
    while (!m_globalPullQueue.empty())
    {
    Ptr<NdpSocket> socket = m_globalPullQueue.front();
    m_globalPullQueue.pop_front();

        auto creditIt = m_pullCredits.find(socket);
        if (creditIt == m_pullCredits.end() || creditIt->second == 0)
        {
            // Stale entry: socket was cancelled or credits pre-emptively removed
            if (creditIt != m_pullCredits.end())
            {
                m_pullCredits.erase(creditIt);
            }
            continue;  // try next socket
        }

        // Consume one credit
        creditIt->second--;
        if (creditIt->second > 0)
        {
            // Still has credits: re-insert at BACK for next round-robin turn
            m_globalPullQueue.push_back(socket);
        }
        else
        {
            // Credits exhausted: remove from map (will be re-added by next EnqueuePull)
            m_pullCredits.erase(creditIt);
        }

        // ✅ Count AFTER verifying the send will actually happen
    if (socket)
    {
        g_pull_sent++;
        socket->SendOnePull();
    }

        // Schedule next PULL if there are more active sockets
    if (!m_globalPullQueue.empty())
    {
            double bitsPerPacket = static_cast<double>(m_pullMtu) * 8.0;
            double bitsPerSecond = m_pullLinkRate.GetBitRate();
            Time   interval      = Seconds(bitsPerPacket / bitsPerSecond);
            m_globalPullEvent    = Simulator::Schedule(interval,
                                                     &NdpL4Protocol::SendGlobalPulls,
                                                     this);
    }
        return;  // done for this tick
    }
    // Queue is empty: timer stops; next EnqueuePull() will restart it.
}

void
NdpL4Protocol::SetPullLinkRate(DataRate rate)
{
    NS_LOG_FUNCTION(this << rate);
    m_pullLinkRate = rate;
}

DataRate
NdpL4Protocol::GetPullLinkRate() const
{
    return m_pullLinkRate;
}

void
NdpL4Protocol::SetPullMtu(uint32_t mtu)
{
    NS_LOG_FUNCTION(this << mtu);
    m_pullMtu = mtu;
}

} // namespace ns3
 