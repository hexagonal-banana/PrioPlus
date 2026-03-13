#include "ndp-socket.h"

#include "ndp-l4-protocol.h"

#include "ns3/inet-socket-address.h"
#include "ns3/ipv4.h"
#include "ns3/ipv4-packet-info-tag.h"
#include "ns3/log.h"
#include "ns3/node.h"
#include "ns3/packet.h"
#include "ns3/random-variable-stream.h"
#include "ns3/simulator.h"
#include "ns3/uinteger.h"
#include "ns3/double.h"
#include "ns3/global-value.h"
#include "ns3/boolean.h"
#include <fstream>
#include <chrono>
#include <iomanip>
#include <set>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("NdpSocket");

NS_OBJECT_ENSURE_REGISTERED(NdpSocket);

// ══════════════════════════════════════════════════════════════════════════════
// Global simulation-wide counters for NDP socket / transport-layer events.
// Printed at simulation end via Simulator::ScheduleDestroy (see ndp-l4-protocol.cc).
// ══════════════════════════════════════════════════════════════════════════════
static uint64_t g_sock_sent          = 0;  ///< new data pkts sent (first transmission)
static uint64_t g_sock_retx          = 0;  ///< retransmitted data pkts (RTX queue)
static uint64_t g_sock_acked         = 0;  ///< ACKs received (sender side)
static uint64_t g_sock_nack_rx       = 0;  ///< NACKs received (sender side; trim notifications)
static uint64_t g_sock_pull_credits  = 0;  ///< PULL credits consumed (packets sent on pull)
static uint64_t g_sock_pull_calls    = 0;  ///< total ProcessPull calls
static uint64_t g_sock_pull_delta0   = 0;  ///< ProcessPull with delta=0 (out-of-order / dup)
static uint64_t g_sock_pull_wasted   = 0;  ///< credits wasted (nothing to send)
static uint64_t g_sock_rto_watchdog  = 0;  ///< RTO watchdog fires (pkt already in rtxQueue)
static uint64_t g_sock_rto_trueloss  = 0;  ///< RTO true-loss fires (no NACK/ACK arrived)
static uint64_t g_sock_rto_gaveup    = 0;  ///< packets abandoned after MAX_RTO_RETRIES
static uint64_t g_sock_data_rx       = 0;  ///< data pkts received at receiver (full pkt)
static uint64_t g_sock_trim_rx       = 0;  ///< trim headers received at receiver
static uint64_t g_sock_flows_started = 0;  ///< flows started (sender sockets Connect()'d)
static uint64_t g_sock_flows_done    = 0;  ///< flows completed (txBuffer drained)
static uint64_t g_sock_fwd_disposed  = 0;  ///< pkts arriving at ForwardUp() after socket disposed

void PrintNdpSocketStats()
{
    std::cout
        << "╔══════════════════════════════════════════════════╗\n"
        << "║          NDP Transport Layer Statistics          ║\n"
        << "╠══════════════════════════════════════════════════╣\n"
        << "║  Flows started              : " << std::setw(8) << g_sock_flows_started   << "              ║\n"
        << "║  Flows completed            : " << std::setw(8) << g_sock_flows_done      << "              ║\n"
        << "╠══════════════════════════════════════════════════╣\n"
        << "║  --- Sender side ---                             ║\n"
        << "║  New data pkts sent         : " << std::setw(8) << g_sock_sent            << "              ║\n"
        << "║  Retransmitted pkts         : " << std::setw(8) << g_sock_retx            << "              ║\n"
        << "║  ACKs received              : " << std::setw(8) << g_sock_acked           << "              ║\n"
        << "║  NACKs received (trims)     : " << std::setw(8) << g_sock_nack_rx         << "              ║\n"
        << "║  PULL credits consumed      : " << std::setw(8) << g_sock_pull_credits    << "              ║\n"
        << "║  PULL ProcessPull calls     : " << std::setw(8) << g_sock_pull_calls     << "              ║\n"
        << "║  PULL delta=0 (dup/reorder) : " << std::setw(8) << g_sock_pull_delta0    << "              ║\n"
        << "║  PULL wasted (no data)      : " << std::setw(8) << g_sock_pull_wasted    << "              ║\n"
        << "╠══════════════════════════════════════════════════╣\n"
        << "║  --- RTO ---                                     ║\n"
        << "║  RTO watchdog fires         : " << std::setw(8) << g_sock_rto_watchdog    << "              ║\n"
        << "║  RTO true-loss fires        : " << std::setw(8) << g_sock_rto_trueloss    << "              ║\n"
        << "║  Pkts abandoned (gave up)   : " << std::setw(8) << g_sock_rto_gaveup      << "              ║\n"
        << "╠══════════════════════════════════════════════════╣\n"
        << "║  --- Receiver side ---                           ║\n"
        << "║  Full data pkts received    : " << std::setw(8) << g_sock_data_rx         << "              ║\n"
        << "║  Trim headers received      : " << std::setw(8) << g_sock_trim_rx         << "              ║\n"
        << "╠══════════════════════════════════════════════════╣\n"
        << "║  --- Orphan / late packets ---                   ║\n"
        << "║  Pkts after socket disposed : " << std::setw(8) << g_sock_fwd_disposed    << "              ║\n"
        << "╚══════════════════════════════════════════════════╝\n";
}
 
 TypeId
 NdpSocket::GetTypeId()
 {
     static TypeId tid =
         TypeId("ns3::NdpSocket")
             .SetParent<Socket>()
             .SetGroupName("Dcb")
             .AddConstructor<NdpSocket>()
             .AddAttribute("FirstWindowSize",
                           "Number of packets to send in first RTT (push phase)",
                           UintegerValue(10),
                           MakeUintegerAccessor(&NdpSocket::m_firstWindowSize),
                           MakeUintegerChecker<uint32_t>())
            .AddAttribute("RtoMs",
                          "RTO timeout in milliseconds (last-resort retransmission timer)",
                          DoubleValue(1.0),
                          MakeDoubleAccessor(&NdpSocket::m_rtoMs),
                          MakeDoubleChecker<double>(0.001, 100.0))
            .AddTraceSource("Tx",
                            "Transmit a packet",
                            MakeTraceSourceAccessor(&NdpSocket::m_txTrace),
                            "ns3::Packet::TracedCallback")
            .AddTraceSource("Rx",
                            "Receive a packet",
                            MakeTraceSourceAccessor(&NdpSocket::m_rxTrace),
                            "ns3::Packet::TracedCallback");
    return tid;
 }
 
// ── NdpSocket::Stats implementation ──────────────────────────────────────────

NdpSocket::Stats::Stats()
    : tStart(Time(0)),
      tFinish(Time(0)),
      tFct(Time(0)),
      overallFlowRate(DataRate(0))
{
    BooleanValue bv;
    if (GlobalValue::GetValueByNameFailSafe("detailedSenderStats", bv))
        bDetailedSenderStats = bv.Get();
    if (GlobalValue::GetValueByNameFailSafe("detailedRetxStats", bv))
        bDetailedRetxStats = bv.Get();
}

void
NdpSocket::Stats::RecordSentPkt(uint32_t size)
{
    nTotalSentPkts++;
    nTotalSentBytes += size;
    if (bDetailedSenderStats)
    {
        vSentPkt.emplace_back(Simulator::Now(), size);
    }
}

void
NdpSocket::Stats::RecordRecvAck(uint32_t seq)
{
    acksReceived++;
    if (bDetailedRetxStats)
    {
        vRecvAck.emplace_back(Simulator::Now(), seq);
    }
}

void
NdpSocket::Stats::RecordRecvNack(uint32_t seq)
{
    nacksReceived++;
    if (bDetailedRetxStats)
    {
        vRecvNack.emplace_back(Simulator::Now(), seq);
    }
}

void
NdpSocket::Stats::CollectAndCheck()
{
    if (tStart.IsStrictlyPositive() && tFinish.IsStrictlyPositive())
    {
        tFct = tFinish - tStart;
        if (tFct.GetSeconds() > 0)
        {
            overallFlowRate = DataRate(nTotalSizeBytes * 8.0 / tFct.GetSeconds());
        }
    }
}

std::shared_ptr<NdpSocket::Stats>
NdpSocket::GetStats() const
{
    m_stats->CollectAndCheck();
    return m_stats;
}

// ── NdpSocket ────────────────────────────────────────────────────────────────

NdpSocket::NdpSocket()
    : m_state(CLOSED),
      m_connectionId(0),
      m_localPort(0),
      m_remotePort(0),
      m_nextSeq(0),
      m_firstSeq(0),
      m_lastSeq(0),
      m_firstWindowSize(10),
      m_lastPullSeq(0),
      m_inPushPhase(true),
      m_rtsCount(0),
      m_expectedSeq(0),
      m_pullSeq(0),
      m_pathIndex(0),
      m_pathProbeInterval(Seconds(1.0)),
      m_errno(ERROR_NOTERROR),
      m_stats(std::make_shared<Stats>())
{
    NS_LOG_FUNCTION(this);
    m_rtoJitter = CreateObject<UniformRandomVariable>();
}
 
NdpSocket::~NdpSocket()
{
     NS_LOG_FUNCTION(this);
}

void
NdpSocket::SetFlowCompleteCallback(Callback<void, Ptr<NdpSocket>> cb)
{
    NS_LOG_FUNCTION(this);
    m_flowCompleteCallback = cb;
}
 
void
NdpSocket::DoDispose()
{
    NS_LOG_FUNCTION(this);

    // ── Idempotency guard ───────────────────────────────────────────────────
    // ns3's Object::DoDelete() calls DoDispose() directly (without going through
    // Object::Dispose() which sets m_disposed).  This means DoDispose() can be
    // called a second time if the socket's reference-count hits zero again after
    // the first DoDispose has already run (e.g. after the scheduled Close-cleanup
    // event fires at t+2ms and then NdpL4Protocol::DoDispose() triggers a second
    // DoDelete when it clears its socket containers).
    //
    // We guard against this by checking whether m_node has already been nulled.
    // If it has, just call Socket::DoDispose() (a harmless no-op on null callbacks)
    // and return, so that Object::DoDelete()'s subsequent `delete this` runs the
    // C++ destructor chain cleanly.
    if (m_node == nullptr)
    {
        Socket::DoDispose();
        return;
    }

    // Cancel all in-flight timers BEFORE nulling m_node/m_ndp.
    // NOTE: We deliberately do NOT call CancelPullsForSocket() here.
    // Calling it re-entrantly (while NdpL4Protocol is clearing its containers)
    // causes heap corruption via re-entrant deque modification.
    // Pull queue cleanup is handled either:
    //  a) by NdpL4Protocol::DoDispose() which clears m_globalPullQueue first
    //     (before triggering socket disposal via m_socketList.clear()), or
    //  b) by NdpSocket::Close() which calls CancelPullsForSocket() safely
    //     before the socket is removed from NdpL4Protocol's containers.
    if (m_ndp)
    {
        m_ndp->UnregisterConnId(m_connectionId);
    }
    if (m_pathProbeEvent.IsRunning())
    {
        m_pathProbeEvent.Cancel();
    }
    // Cancel all per-seq RTO timers
    for (auto& kv : m_rtoTimers)
    {
        kv.second.Cancel();
    }
    m_rtoTimers.clear();

    m_ndp = nullptr;
    m_node = nullptr;
    m_txQueue.clear();
    m_rtxQueue.clear();
    m_txBuffer.clear();
    m_rxBuffer.clear();
    m_paths.clear();
    m_pathOrder.clear();
    m_pathScores.clear();
     
     Socket::DoDispose();
 }
 
 void
 NdpSocket::SetNdp(Ptr<NdpL4Protocol> ndp)
 {
     m_ndp = ndp;
     m_node = ndp->GetNode();
 }
 
 enum Socket::SocketErrno
 NdpSocket::GetErrno() const
 {
     return m_errno;
 }
 
 enum Socket::SocketType
 NdpSocket::GetSocketType() const
 {
     return NS3_SOCK_STREAM;
 }
 
 Ptr<Node>
 NdpSocket::GetNode() const
 {
     return m_node;
 }
 
int
NdpSocket::Bind()
{
    NS_LOG_FUNCTION(this);
    
    // Set m_localAddress to the node's primary IP
    if (m_node)
    {
        Ptr<Ipv4> ipv4 = m_node->GetObject<Ipv4>();
        if (ipv4 && ipv4->GetNInterfaces() > 1)
        {
            m_localAddress = ipv4->GetAddress(1, 0).GetLocal();
        }
        else
        {
            m_localAddress = Ipv4Address("0.0.0.0");
        }
    }
    else
    {
        m_localAddress = Ipv4Address("0.0.0.0");
    }
    
    m_localPort = m_ndp->AllocatePort();
    m_ndp->RegisterSocketWithPort(this, m_localPort);
    m_state = CLOSED;
    return 0;
}
 
 int
 NdpSocket::Bind6()
 {
     return -1; // IPv6 not supported
 }
 
 int
 NdpSocket::Bind(const Address& address)
 {
     NS_LOG_FUNCTION(this << address);
     
     if (!InetSocketAddress::IsMatchingType(address))
     {
         m_errno = ERROR_INVAL;
         return -1;
     }
     
    InetSocketAddress addr = InetSocketAddress::ConvertFrom(address);
    m_localAddress = addr.GetIpv4();
    m_localPort = addr.GetPort();

    //自动端口分配
    if (m_localPort == 0)
    {
        m_localPort = m_ndp->AllocatePort();
    }
    
    m_ndp->RegisterSocketWithPort(this, m_localPort);  // Register port mapping
    m_state = CLOSED;
    return 0;
 }
 
 int
 NdpSocket::Close()
 {
     NS_LOG_FUNCTION(this);
     
     if (m_state == ESTABLISHED)
     {
         // Send LAST packet
         NdpHeader header;
         header.SetConnectionId(m_connectionId);
         header.SetSequence(m_nextSeq);
         header.AddFlag(NdpHeader::LAST);
         
         Ptr<Packet> packet = Create<Packet>(0);
         packet->AddHeader(header);
         
         // Send on any path
         if (!m_paths.empty())
         {
             m_ndp->Send(packet, m_localAddress, m_paths[0], m_localPort, m_remotePort);
         }
         
         m_state = TIME_WAIT;
     }

     // ── Immediate cleanup ────────────────────────────────────────────────
     // Cancel all in-flight timers now so that their EventImpl callbacks
     // (which hold Ptr<NdpSocket> to this socket) are released as each
     // cancelled event fires, before Simulator::Destroy() runs.
     if (m_pathProbeEvent.IsRunning())
     {
         m_pathProbeEvent.Cancel();
     }
     for (auto& kv : m_rtoTimers)
     {
         kv.second.Cancel();
     }
     m_rtoTimers.clear();

     // Deregister from NdpL4Protocol NOW so that NdpL4Protocol::DoDispose()
     // won't encounter this socket in its container clearing and trigger a
     // second DoDelete/DoDispose on an already-cleaned-up socket.
     if (m_ndp)
     {
         m_ndp->CancelPullsForSocket(this);
         m_ndp->UnregisterSocket(this);
         m_ndp->UnregisterConnId(m_connectionId);
     }

     m_state = CLOSED;
     
     return 0;
 }
 
 int
 NdpSocket::ShutdownSend()
 {
     NS_LOG_FUNCTION(this);
     return Close();
 }
 
 int
 NdpSocket::ShutdownRecv()
 {
     NS_LOG_FUNCTION(this);
     return 0;
 }
 
 int
 NdpSocket::Connect(const Address& address)
 {
     NS_LOG_FUNCTION(this << address);
     
     if (!InetSocketAddress::IsMatchingType(address))
     {
         m_errno = ERROR_INVAL;
         return -1;
     }
     
     InetSocketAddress addr = InetSocketAddress::ConvertFrom(address);
     m_remoteAddress = addr.GetIpv4();
     m_remotePort = addr.GetPort();
     
    if (m_localPort == 0)
    {
        m_localPort = m_ndp->AllocatePort();
        m_ndp->RegisterSocketWithPort(this, m_localPort);  // Register port mapping
    }

    // ✅ BUG FIX: Ensure m_localAddress is set before computing connId.
    // Connect() may be called without a prior Bind(), leaving m_localAddress as 0.0.0.0.
    if (m_localAddress == Ipv4Address("0.0.0.0") && m_node)
    {
        Ptr<Ipv4> ipv4 = m_node->GetObject<Ipv4>();
        if (ipv4 && ipv4->GetNInterfaces() > 1)
        {
            m_localAddress = ipv4->GetAddress(1, 0).GetLocal();
        }
    }

    // ✅ BUG FIX: Include source IP in connection ID to prevent collisions.
    //
    // Old encoding: (srcPort << 16) | dstPort
    //   → All senders connecting to the same dstPort with identical srcPort
    //     (e.g., all nodes independently allocate port 49152) share the same connId,
    //     causing the receiver to route all flows to the first accepted socket.
    //
    // New encoding: (srcIP << 32) | (srcPort << 16) | dstPort
    //   → srcIP is unique per node, guaranteeing per-flow uniqueness.
    //   → NdpL4Protocol::Receive() port-extraction remains compatible:
    //       senderLocalPort  = (connId >> 16) & 0xFFFF  ← still extracts srcPort correctly
    //       senderRemotePort = connId & 0xFFFF           ← still extracts dstPort correctly
    uint32_t localIp = m_localAddress.Get();
    m_connectionId = (static_cast<uint64_t>(localIp) << 32) |
                     (static_cast<uint64_t>(m_localPort) << 16) |
                     static_cast<uint64_t>(m_remotePort);
    
    m_firstSeq = 0;
     m_nextSeq = 0;
     
     m_state = SYN_SENT;
     m_inPushPhase = true;
     
     // Initialize path order
     if (!m_paths.empty())
     {
         m_pathOrder.clear();
         for (size_t i = 0; i < m_paths.size(); i++)
         {
             m_pathOrder.push_back(i);//为每条路径创建索引
             m_pathScores[i] = NdpPathScore();//初始化每条路径的健康度评分
         }
         
         // Random permutation
         //使用随机变量对路径顺序进行随机排列
         Ptr<UniformRandomVariable> rand = CreateObject<UniformRandomVariable>();
         for (size_t i = m_pathOrder.size() - 1; i > 0; i--)
         {
             size_t j = rand->GetInteger(0, i);
             std::swap(m_pathOrder[i], m_pathOrder[j]);
         }
        m_pathIndex = 0;
    }
    
    // CRITICAL FIX: Set state BEFORE notifying success
    // Application will try to send data in the callback, so state must be ESTABLISHED
    m_state = ESTABLISHED;
    g_sock_flows_started++;
    NotifyConnectionSucceeded();
    
    // Start sending first window if we have data
    if (!m_txQueue.empty())
    {
        SendFirstWindow();
    }

    return 0;
 }
 
 int
 NdpSocket::Listen()
 {
     NS_LOG_FUNCTION(this);
     m_state = LISTEN;
     return 0;
 }
 
 //查询可用的发送缓冲区大小，NDP不限制发送缓冲区大小
 uint32_t
 NdpSocket::GetTxAvailable() const
 {
     return 0xFFFFFFFF; // Unlimited for now
 }
 
int
NdpSocket::Send(Ptr<Packet> p, uint32_t flags)
{
    NS_LOG_FUNCTION(this << p << flags);
    
    // std::cout << "  🔹 [SOCKET-TX] Node=" << m_node->GetId()
    //           << " State=" << (m_state == ESTABLISHED ? "ESTAB" : "OTHER")
    //           << " PushPhase=" << m_inPushPhase
    //           << " TxQSize=" << m_txQueue.size()
    //           << " PktSize=" << p->GetSize() << "B"
    //           << std::endl;
    
    NS_LOG_DEBUG("Send called: state=" << m_state 
                 << " inPushPhase=" << m_inPushPhase 
                 << " nextSeq=" << m_nextSeq 
                 << " firstSeq=" << m_firstSeq 
                 << " firstWindowSize=" << m_firstWindowSize 
                 << " txQueue.size=" << m_txQueue.size()
                 << " packet.size=" << p->GetSize());
    
    if (m_state != ESTABLISHED)
    {
        NS_LOG_WARN("Send failed: socket not established (state=" << m_state << ")");
        m_errno = ERROR_NOTCONN;
        return -1;
    }
    
    if (!m_ndp)
    {
        NS_LOG_WARN("Send failed: NdpL4Protocol not set");
        m_errno = ERROR_NOTERROR;
        return -1;
    }
    
    // Add to transmit queue
    m_txQueue.push_back(p);
    m_lastSeq = m_firstSeq + m_txQueue.size();
    
    NS_LOG_DEBUG("After push: txQueue.size=" << m_txQueue.size() 
                 << " lastSeq=" << m_lastSeq);
    
    // If in push phase and haven't sent first window, send it
    if (m_inPushPhase && m_nextSeq < m_firstSeq + m_firstWindowSize)
    {
        NS_LOG_DEBUG("Calling SendFirstWindow()");
        SendFirstWindow();
    }
    
    return p->GetSize();
}
 
 int
 NdpSocket::SendTo(Ptr<Packet> p, uint32_t flags, const Address& address)
 {
     return Send(p, flags);
 }
 
 //只处理掉乱序的数据包，没有对新数据包的处理
 Ptr<Packet>
 NdpSocket::Recv(uint32_t maxSize, uint32_t flags)
 {
     NS_LOG_FUNCTION(this << maxSize << flags);
     
     if (m_rxBuffer.empty())
     {
         return nullptr;
     }
     
     // Return the first in-order packet
     auto it = m_rxBuffer.find(m_expectedSeq);
     if (it != m_rxBuffer.end())
     {
         Ptr<Packet> packet = it->second;
         m_rxBuffer.erase(it);
         m_expectedSeq++;
         return packet;
     }
     
     return nullptr;
 }
 
 Ptr<Packet>
 NdpSocket::RecvFrom(uint32_t maxSize, uint32_t flags, Address& fromAddress)
 {
     Ptr<Packet> packet = Recv(maxSize, flags);
     if (packet)
     {
         fromAddress = InetSocketAddress(m_remoteAddress, m_remotePort);
     }
     return packet;
 }
 
 //查询接收缓冲区中可读的数据量
 uint32_t
 NdpSocket::GetRxAvailable() const
 {
     uint32_t total = 0;
     for (const auto& pair : m_rxBuffer)
     {
         total += pair.second->GetSize();
     }
     return total;
 }
 
 //获取本地 Socket 地址（本地绑定的地址和端口）
 int
 NdpSocket::GetSockName(Address& address) const
 {
     address = InetSocketAddress(m_localAddress, m_localPort);
     return 0;
 }
 
 //获取对端 Socket 地址（连接的远程地址和端口）
 int
 NdpSocket::GetPeerName(Address& address) const
 {
     address = InetSocketAddress(m_remoteAddress, m_remotePort);
     return 0;
 }
 
 bool
 NdpSocket::SetAllowBroadcast(bool allowBroadcast)
 {
     return false; // Not supported
 }
 
 bool
 NdpSocket::GetAllowBroadcast() const
 {
     return false;
 }
 
void
NdpSocket::ForwardUp(Ptr<Packet> packet,
                    Ipv4Header header,
                    uint16_t port,
                    Ptr<Ipv4Interface> incomingInterface)
{
    NS_LOG_FUNCTION(this << packet << header << port);
    
    // Guard: socket may have been disposed (m_node = nullptr after DoDispose)
    // Packets still in-flight after socket close must be silently dropped
    if (m_node == nullptr || m_ndp == nullptr)
    {
        g_sock_fwd_disposed++;
        NS_LOG_DEBUG("ForwardUp: socket disposed, dropping packet (total=" << g_sock_fwd_disposed << ")");
        return;
    }
    
    // Initialize addresses from Ipv4Header if not set
    if (m_remoteAddress == Ipv4Address("0.0.0.0") || m_remoteAddress == Ipv4Address("102.102.102.102"))
    {
        m_remoteAddress = header.GetSource();
        m_remotePort = port;
    }
    if (m_localAddress == Ipv4Address("0.0.0.0"))
    {
        m_localAddress = header.GetDestination();
    }
    
    // Extract NDP header
    NdpHeader ndpHeader;
    packet->RemoveHeader(ndpHeader);
    
    NS_LOG_DEBUG("Received NDP packet: connId="
                 << ndpHeader.GetConnectionId() << " seq=" << ndpHeader.GetSequence()
                 << " flags=0x" << std::hex << (int)ndpHeader.GetFlags() << std::dec);
     
    // Check for return-to-sender (RTS) packet
    // RTS is indicated by TRIM flag on a packet coming from unexpected source
    // ✅ NEW: Check for Return-to-Sender (RTS) using dedicated RTS flag
    // RTS = TRIM + RTS flag (set by switch when both LowQueue and HighQueue are full)
    // This distinguishes RTS from receiver's NACK
    bool isReturnToSender = false;
    if (ndpHeader.IsRts() && m_state == ESTABLISHED)
    {
        // RTS detected: packet was trimmed at switch due to congestion
        // and returned to sender
        isReturnToSender = true;
        m_rtsCount++;
        
        uint32_t nodeId = m_node->GetId();
        uint32_t seq = ndpHeader.GetSequence();
        uint8_t pathId = ndpHeader.GetPathId();
        Time now = Simulator::Now();
        
        // 🔍 DEBUG: RTS received
        // std::cout << "📥 [RTS-RX] Node=" << nodeId
        //           << " Time=" << now.GetMicroSeconds() << "us"
        //           << " Seq=" << seq
        //           << " PathId=" << (int)pathId
        //           << " TotalRTS=" << m_rtsCount
        //           << " From=" << header.GetSource()
        //           << " To=" << header.GetDestination()
        //           << std::endl;
        
        // ✅ SPEC: Sender收到RTS → 放入RTX buffer + 等待PULL，⚠️绝不立即重传
        // "Sender不能立即重传！否则会incast echo。"
        // Duplicate check: only add to RTX queue if not already there
        bool alreadyQueued = false;
        for (const auto& rtxSeq : m_rtxQueue)
        {
            if (rtxSeq == seq)
            {
                alreadyQueued = true;
                break;
            }
        }
        
        if (!alreadyQueued)
        {
            // Push to FRONT of RTX queue (high priority vs NACK which pushes to back)
            m_rtxQueue.push_front(seq);
            
            // std::cout << "  ⏳ [RTS-WAIT-PULL] Node=" << nodeId
            //           << " Seq=" << seq
            //           << " RTXQueueSize=" << m_rtxQueue.size()
            //           << " LastPullSeq=" << m_lastPullSeq
            //           << " PathId=" << (int)pathId
            //           << " (queued for RTX, waiting for PULL)"
            //           << " @" << now.GetMicroSeconds() << "us"
            //           << std::endl;
        }
        else
        {
            // std::cout << "  ⚠️ [RTS-ALREADY-QUEUED] Node=" << nodeId
            //           << " Seq=" << seq
            //           << " RTXQueueSize=" << m_rtxQueue.size()
            //           << " (duplicate RTS, already in RTX queue)"
            //           << std::endl;
        }
        
        // ✅ RTO: Cancel the 1ms timeout (RTS = feedback received, RTX queued)
        CancelRto(seq);
        
        // Update path health: RTS means this path had congestion
        UpdatePathHealth(pathId, false);
    }
    
    // Process based on packet type (skip if RTS already handled)
    if (!isReturnToSender)
    {
        if (ndpHeader.IsAck())
        {
            ProcessAck(ndpHeader);
        }
        else if (ndpHeader.IsNack())
        {
            ProcessNack(ndpHeader);
        }
        else if (ndpHeader.IsPull())
        {
            ProcessPull(ndpHeader);
        }
        else if (ndpHeader.IsData())
        {
            ProcessData(packet, ndpHeader);
        }
    }
     
     // Handle SYN for connection establishment
     if (ndpHeader.IsSyn() && m_state == LISTEN)
     {
        m_remoteAddress = header.GetSource();
        m_remotePort = port;
        m_connectionId = ndpHeader.GetConnectionId();
        m_firstSeq = ndpHeader.GetSequence() - ndpHeader.GetSeqOffset();
        m_expectedSeq = m_firstSeq;
        m_state = ESTABLISHED;
        
        NotifyConnectionSucceeded();
    }
     
    // Handle LAST flag
    if (ndpHeader.IsLast())
    {
        // Cancel any pending pull credits for this flow in the global queue.
        if (m_ndp)
        {
            m_ndp->CancelPullsForSocket(this);
        }
    }
}
 
 void
 NdpSocket::SetPaths(const std::vector<Ipv4Address>& paths)
 {
     NS_LOG_FUNCTION(this << paths.size());
     m_paths = paths;
     
     // Initialize path scores
     for (size_t i = 0; i < m_paths.size(); i++)
     {
         m_pathScores[i] = NdpPathScore();
     }
 }
 
uint64_t
NdpSocket::GetConnectionId() const
{
    return m_connectionId;
}

void
NdpSocket::SendFirstWindow()
{
    NS_LOG_FUNCTION(this);
    
    if (!m_inPushPhase)
    {
        NS_LOG_DEBUG("Not in push phase, skipping first window");
        return;
    }
    
    // NDP specification: send entire first window immediately (zero-RTT push)
    // Send packets back-to-back without pacing, waiting for ACK, or waiting for PULL
    uint32_t windowEnd = m_firstSeq + m_firstWindowSize;
    
    NS_LOG_DEBUG("SendFirstWindow: sending packets " << m_nextSeq << " to " << windowEnd
                 << " (txQueue.size=" << m_txQueue.size() << ")");
    
    // Send all packets in first window at once
    while (m_nextSeq < windowEnd && m_nextSeq < m_lastSeq)
    {
        SendDataPacket(m_nextSeq, false);
        m_nextSeq++;
    }
    
    // After first window, transition to pull phase
    if (m_nextSeq >= m_firstSeq + m_firstWindowSize)
    {
        m_inPushPhase = false;
        NS_LOG_INFO("Completed first window (" << m_firstWindowSize 
                    << " packets), entering pull phase");
    }
}
 
void
NdpSocket::SendDataPacket(uint32_t seq, bool isRetransmit, uint8_t avoidPath)
{
    NS_LOG_FUNCTION(this << seq << isRetransmit << (int)avoidPath);
    
    uint32_t nodeId = m_node->GetId();
    Time now = Simulator::Now();
    
    uint32_t index = seq - m_firstSeq;
    if (index >= m_txQueue.size())
    {
        NS_LOG_WARN("Sequence " << seq << " out of range (index=" << index 
                    << ", txQueue.size=" << m_txQueue.size() << ")");
        return;
    }
    
    Ptr<Packet> payload = m_txQueue[index]->Copy();
    
    // Create NDP header
    NdpHeader header;
    header.SetConnectionId(m_connectionId);
    header.SetSequence(seq);
    
    // Set SYN flag and seq_offset for first-window packets.
    // CRITICAL: Use seq range check instead of m_inPushPhase flag.
    // m_inPushPhase is cleared after SendFirstWindow() completes, but if
    // first-window packets were dropped at the NIC queue and later
    // retransmitted via RTO, the SYN flag must still be present so the
    // receiver can create an accepted socket for this connection.
    if (seq < m_firstSeq + m_firstWindowSize)
    {
        header.AddFlag(NdpHeader::SYN);
        header.SetSeqOffset(seq - m_firstSeq);
    }
    
    // Select path using multipath routing
    uint8_t pathId = SelectPath(avoidPath);
    header.SetPathId(pathId);
    
    payload->AddHeader(header);
    
    // ── Global stats ──────────────────────────────────────────────────────────
    if (isRetransmit) { g_sock_retx++; m_stats->nRetxCount++; } else { g_sock_sent++; }
    m_stats->RecordSentPkt(payload->GetSize());

    // 🔍 DEBUG: Track retransmissions
    if (isRetransmit)
    {
        static std::map<uint32_t, std::map<uint32_t, uint32_t>> rtxCount;  // nodeId -> seq -> count
        rtxCount[nodeId][seq]++;
        
        // std::cout << "🔄 [RTX-SEND] Node=" << nodeId
        //           << " Time=" << now.GetMicroSeconds() << "us"
        //           << " Seq=" << seq
        //           << " PathId=" << (int)pathId
        //           << " AvoidPath=" << (int)avoidPath
        //           << " RTX#=" << rtxCount[nodeId][seq]
        //           << " Size=" << payload->GetSize() << "B"
        //           << " DstIP=" << (pathId < m_paths.size() ? m_paths[pathId] : Ipv4Address("0.0.0.0"))
        //           << " RTXQueueSize=" << m_rtxQueue.size()
        //           << std::endl;
    }
    else
    {
        // First transmission
        // std::cout << "📤 [TX-SEND] Node=" << nodeId
        //           << " Time=" << now.GetMicroSeconds() << "us"
        //           << " Seq=" << seq
        //           << " PathId=" << (int)pathId
        //           << " Size=" << payload->GetSize() << "B"
        //           << " DstIP=" << (pathId < m_paths.size() ? m_paths[pathId] : Ipv4Address("0.0.0.0"))
        //           << " NextSeq=" << m_nextSeq << "/" << m_lastSeq
        //           << std::endl;
    }
    
    // Send packet on selected path
    if (pathId < m_paths.size())
    {
        m_ndp->Send(payload, m_localAddress, m_paths[pathId], m_localPort, m_remotePort);
        m_txTrace(payload);
        
        // Store in tx buffer (Outstanding Table) for retransmission tracking
        m_txBuffer[seq] = NdpTxItem(seq, m_txQueue[index]->Copy(), Simulator::Now(), pathId);
        m_txBuffer[seq].retransmitted = isRetransmit;
        
        // ✅ RTO: Start/reset 1ms per-seq timeout (last-resort fallback).
        //
        // ScheduleRto() cancels any existing timer for this seq before arming a new one,
        // so there is always exactly ONE RTO timer per outstanding sequence number.
        // Resetting the timer on a retransmit is correct: if the retransmit is also
        // lost we want another 1ms window before the next rescue attempt.
        ScheduleRto(seq);

        NS_LOG_DEBUG("Sent data packet: seq=" << seq << " pathId=" << (int)pathId
                                              << " retx=" << isRetransmit << " → RTO armed (1ms)");
    }
    else
    {
        NS_LOG_ERROR("Invalid pathId=" << (int)pathId << " (paths.size=" << m_paths.size() << ")");
    }
}
 
 void
 NdpSocket::ProcessAck(const NdpHeader& header)
 {
     NS_LOG_FUNCTION(this << header.GetSequence());
     
     uint32_t nodeId = m_node->GetId();
     uint32_t seq = header.GetSequence();
     uint8_t pathId = header.GetPathId();
     Time now = Simulator::Now();
     
     // ── Global stats ──────────────────────────────────────────────────────────
    g_sock_acked++;
    m_stats->RecordRecvAck(seq);

    // 🔍 DEBUG: ACK received (packet successfully delivered)
     static std::map<uint32_t, uint64_t> ackCount;  // nodeId -> count
     ackCount[nodeId]++;
     
     bool wasRetransmitted = false;
     auto it = m_txBuffer.find(seq);
     if (it != m_txBuffer.end())
     {
         wasRetransmitted = it->second.retransmitted;
     }
     
    //  std::cout << "✅ [ACK-RX] Node=" << nodeId
    //            << " Time=" << now.GetMicroSeconds() << "us"
    //            << " Seq=" << seq
    //            << " PathId=" << (int)pathId
    //            << " WasRTX=" << wasRetransmitted
    //            << " TotalACKs=" << ackCount[nodeId]
    //            << " TXBufferSize=" << m_txBuffer.size()
    //            << std::endl;
     
     // Free buffer (remove from Outstanding Table)
     if (it != m_txBuffer.end())
     {
         m_txBuffer.erase(it);
     }
     
     // ✅ RTO: Cancel the 1ms timeout for this seq (packet confirmed delivered)
     CancelRto(seq);
     
     // ── Flow completion detection ──────────────────────────────────────────
     // A flow is done when all data has been queued (nextSeq == lastSeq) AND
     // the Outstanding Table is empty (every sent packet has been ACK'd).
     if (m_txBuffer.empty() && m_nextSeq >= m_lastSeq && m_lastSeq > 0
         && !m_flowCompleted && !m_moreDataPending)
     {
         m_flowCompleted = true;
         g_sock_flows_done++;
         NS_LOG_INFO("Flow completed (all-ACKed): connId=" << m_connectionId
                     << " totalPkts=" << (m_lastSeq - m_firstSeq)
                     << " @" << Simulator::Now().GetSeconds() << "s");

         if (!m_flowCompleteCallback.IsNull())
         {
             m_flowCompleteCallback(Ptr<NdpSocket>(this));
         }
     }

     // Update path health
     UpdatePathHealth(pathId, true);
     
     NS_LOG_DEBUG("Received ACK: seq=" << seq << " pathId=" << (int)pathId);
 }
 
void
NdpSocket::ProcessNack(const NdpHeader& header)
{
    NS_LOG_FUNCTION(this << header.GetSequence());
    
    uint32_t nodeId = m_node->GetId();
    uint32_t seq = header.GetSequence();
    uint8_t pathId = header.GetPathId();
    Time now = Simulator::Now();
    
    // ✅ 修复1: 如果 seq 已经被 ACK 了（不在 txBuffer），忽略此 NACK
    if (m_txBuffer.find(seq) == m_txBuffer.end())
    {
        NS_LOG_DEBUG("ProcessNack: seq=" << seq << " already ACK'd, ignoring stale NACK");
        return;
    }

    // ── Global stats ──────────────────────────────────────────────────────────
    g_sock_nack_rx++;
    m_stats->RecordRecvNack(seq);

    // ✅ SPEC: NACK表示包被Trim，放入RTX buffer等待PULL，不立即重传
    // Duplicate check: avoid adding same seq multiple times
    bool alreadyQueued = false;
    for (const auto& rtxSeq : m_rtxQueue)
    {
        if (rtxSeq == seq)
        {
            alreadyQueued = true;
            break;
        }
    }
    
    if (!alreadyQueued)
    {
        m_rtxQueue.push_back(seq);  // Push to back (lower priority than RTS)
        
        // std::cout << "📬 [NACK-RX] Node=" << nodeId
        //           << " Time=" << now.GetMicroSeconds() << "us"
        //           << " Seq=" << seq
        //           << " PathId=" << (int)pathId
        //           << " RTXQueueSize=" << m_rtxQueue.size()
        //           << " (queued for RTX, waiting for PULL)"
        //           << std::endl;
    }
    else
    {
        // std::cout << "⚠️ [NACK-DUP] Node=" << nodeId
        //           << " Seq=" << seq
        //           << " (duplicate NACK, already in RTX queue, ignored)"
        //           << std::endl;
    }
    
    // RTO after NACK: the packet is now in the RTX queue waiting for a PULL credit.
    // We KEEP (or re-arm) the RTO as a safety net: if no PULL arrives within
    // the watchdog interval, Case-A will fire a direct retransmit to restart the
    // PULL chain.  Without this, packets in RTX can stall indefinitely when the
    // PULL chain breaks (PULL chain needs packets to arrive to generate PULLs,
    // but packets need PULLs to be sent → circular dependency / deadlock).
    //
    // Use a 10ms watchdog interval (longer than 1ms) to limit event rate while
    // still recovering from broken PULL chains within reasonable time.
    //
    // NOTE: SendDataPacket (called from ProcessPull) will call ScheduleRto(seq)
    // which will reset this timer to 1ms on the next PULL-driven retransmission.
    // The 10ms interval only activates when NO PULL arrives.
    {
        double jitterFrac = (m_rtoJitter->GetValue() - 0.5) * 0.5;
        double watchdogMs = 10.0 * (1.0 + jitterFrac);
        // Cancel existing RTO (might be a 1ms RTO from the original send), then
        // re-arm as a 10ms watchdog for the "stuck in RTX" recovery case.
        CancelRto(seq);
        auto watchdogEvent = Simulator::Schedule(Seconds(watchdogMs * 1e-3),
                                                 &NdpSocket::RtoExpired, this, seq);
        m_rtoTimers[seq] = watchdogEvent;
    }
    
    // Update path health
    UpdatePathHealth(pathId, false);
    
    NS_LOG_DEBUG("Received NACK: seq=" << seq << " pathId=" << (int)pathId
                 << " (rtxQueue.size=" << m_rtxQueue.size() << ")");
}
 
void
NdpSocket::ProcessPull(const NdpHeader& header)
{
    NS_LOG_FUNCTION(this << header.GetPullSequence());
    
    uint32_t nodeId = m_node->GetId();
    uint32_t pullSeq = header.GetPullSequence();
    Time now = Simulator::Now();
    
    // Calculate delta (cumulative PULL)
    uint32_t delta = 0;
    if (pullSeq > m_lastPullSeq)
    {
        delta = pullSeq - m_lastPullSeq;
        m_lastPullSeq = pullSeq;
    }
    
    // 🔍 DEBUG: PULL received
    // std::cout << "📥 [PULL-RX] Node=" << nodeId
    //           << " Time=" << now.GetMicroSeconds() << "us"
    //           << " PullSeq=" << pullSeq
    //           << " Delta=" << delta
    //           << " LastPullSeq=" << (m_lastPullSeq - delta)
    //           << " RTXQueueSize=" << m_rtxQueue.size()
    //           << " NextSeq=" << m_nextSeq << "/" << m_lastSeq
    //           << std::endl;
    
    // ── Global stats ──────────────────────────────────────────────────────────
    g_sock_pull_calls++;
    if (delta == 0) { g_sock_pull_delta0++; }

    NS_LOG_DEBUG("Received PULL: pullSeq=" << pullSeq << " delta=" << delta
                 << " (nextSeq=" << m_nextSeq << ", lastSeq=" << m_lastSeq 
                 << ", rtxQueue.size=" << m_rtxQueue.size() << ")");
    
    // Send delta packets: retransmissions first, then new data
    uint32_t rtxSent = 0;
    uint32_t newDataSent = 0;

    // ✅ 修复2: 在处理每个 PULL credit 前，先清空 rtxQueue 中已 ACK 的 stale 条目
    // 这必须放在循环外（一次性清空），避免在循环内反复扫描
    while (!m_rtxQueue.empty() && m_txBuffer.find(m_rtxQueue.front()) == m_txBuffer.end())
    {
        NS_LOG_DEBUG("ProcessPull: pre-drain stale rtxSeq=" << m_rtxQueue.front() << " (already ACK'd)");
        m_rtxQueue.pop_front();
    }

    for (uint32_t i = 0; i < delta; i++)
    {
        // 每次迭代开始前也检查新的 stale 条目（SendDataPacket 执行期间可能有 ACK 到来）
        while (!m_rtxQueue.empty() && m_txBuffer.find(m_rtxQueue.front()) == m_txBuffer.end())
        {
            NS_LOG_DEBUG("ProcessPull: in-loop drain stale rtxSeq=" << m_rtxQueue.front());
            m_rtxQueue.pop_front();
        }

        if (!m_rtxQueue.empty())
        {
            // Send retransmission (avoid failed path)
            uint32_t rtxSeq = m_rtxQueue.front();
            m_rtxQueue.pop_front();
            auto it = m_txBuffer.find(rtxSeq); // guaranteed valid by drain loop above
            uint8_t avoidPath = it->second.pathId;
            rtxSent++;
            g_sock_pull_credits++;
            m_stats->pullsConsumed++;
            SendDataPacket(rtxSeq, true, avoidPath);
        }
        else if (m_nextSeq < m_lastSeq)
        {
            // Send new data
            newDataSent++;
            g_sock_pull_credits++;
            m_stats->pullsConsumed++;
            SendDataPacket(m_nextSeq, false);
            m_nextSeq++;
        }
        else
        {
            g_sock_pull_wasted += (delta - i); // count all remaining unused credits
            break; // No more data to send
        }
    }
    
    if (delta > 0)
    {
        // std::cout << "  ✅ [PULL-COMPLETE] Node=" << nodeId
        //           << " PullSeq=" << pullSeq
        //           << " Delta=" << delta
        //           << " RTXSent=" << rtxSent
        //           << " NewDataSent=" << newDataSent
        //           << " RTXQueueRemaining=" << m_rtxQueue.size()
        //           << std::endl;
    }
}
 
void
NdpSocket::ProcessData(Ptr<Packet> packet, const NdpHeader& header)
{
    NS_LOG_FUNCTION(this << header.GetSequence());
    
    uint32_t nodeId = m_node->GetId();
    uint32_t seq = header.GetSequence();
    Time now = Simulator::Now();
    
    // 🔍 DEBUG: Data packet received
    static std::map<uint32_t, uint64_t> dataRxCount;  // nodeId -> count
    dataRxCount[nodeId]++;
    
    // ── Global stats ──────────────────────────────────────────────────────────
    if (header.IsTrim()) { g_sock_trim_rx++; } else { g_sock_data_rx++; }

    if (header.IsTrim())
    {
        // Trimmed packet - send NACK
        // std::cout << "✂️ [DATA-TRIM-RX] Node=" << nodeId
        //           << " Time=" << now.GetMicroSeconds() << "us"
        //           << " Seq=" << seq
        //           << " Size=" << packet->GetSize() << "B"
        //           << " TotalDataRX=" << dataRxCount[nodeId]
        //           << " → Sending NACK"
        //           << std::endl;
        
        SendNack(seq);

        NS_LOG_DEBUG("Received TRIM: seq=" << seq << " → Sending NACK");

        // Enqueue one pull credit in the global L4 pull queue.
        if (m_ndp)
        {
            m_ndp->EnqueuePull(this);
        }

        NS_LOG_DEBUG("Received TRIM: seq=" << seq);
    }
    else
    {
        // Normal data packet - store in receive buffer
        // std::cout << "✅ [DATA-RX] Node=" << nodeId
        //           << " Time=" << now.GetMicroSeconds() << "us"
        //           << " Seq=" << seq
        //           << " Size=" << packet->GetSize() << "B"
        //           << " TotalDataRX=" << dataRxCount[nodeId]
        //           << " RXBufferSize=" << m_rxBuffer.size()
        //           << " ExpectedSeq=" << m_expectedSeq
        //           << " → Sending ACK"
        //           << std::endl;

        m_rxBuffer[seq] = packet->Copy();

        // ✅ SPEC: Receiver MUST send ACK when full data packet arrives
        SendAck(seq);

        // Enqueue one pull credit in the global L4 pull queue.
        if (m_ndp)
        {
            m_ndp->EnqueuePull(this);
        }

        // Call callbacks
        NotifyDataRecv();
        m_rxTrace(packet);

        NS_LOG_DEBUG("Received DATA: seq=" << seq << " → sent ACK + enqueued PULL");
    }
}
 
 void
 NdpSocket::SendAck(uint32_t seq)
 {
     NS_LOG_FUNCTION(this << seq);
     
    //  std::cout << "      🔷 [SOCKET-SendACK] Node=" << m_node->GetId()
    //            << " Seq=" << seq
    //            << std::endl;
     
     NdpHeader header;
     header.SetConnectionId(m_connectionId);
     header.SetSequence(seq);
     header.AddFlag(NdpHeader::ACK);
     
     Ptr<Packet> packet = Create<Packet>(0);
     packet->AddHeader(header);
     
     // Always send ACK – accepted (receiver) sockets have no m_paths but still
     // need to send control packets back to the sender.
     m_ndp->Send(packet, m_localAddress, m_remoteAddress, m_localPort, m_remotePort);
 }

 void
 NdpSocket::SendNack(uint32_t seq)
 {
     NS_LOG_FUNCTION(this << seq);
     
    //  std::cout << "      🔶 [SOCKET-SendNACK] Node=" << m_node->GetId()
    //            << " Seq=" << seq
    //            << std::endl;
     
     NdpHeader header;
     header.SetConnectionId(m_connectionId);
     header.SetSequence(seq);
     header.AddFlag(NdpHeader::NACK);
     
     Ptr<Packet> packet = Create<Packet>(0);
     packet->AddHeader(header);
     
     // Always send NACK – accepted (receiver) sockets have no m_paths but still
     // need to send control packets back to the sender.
     m_ndp->Send(packet, m_localAddress, m_remoteAddress, m_localPort, m_remotePort);
 }

 void
 NdpSocket::SendPull(uint32_t pullSeq)
 {
     NS_LOG_FUNCTION(this << pullSeq);
     
    //  std::cout << "      🔵 [SOCKET-SendPULL] Node=" << m_node->GetId()
    //            << " PullSeq=" << pullSeq
    //            << std::endl;
     
     NdpHeader header;
     header.SetConnectionId(m_connectionId);
     header.SetPullSequence(pullSeq);
     header.AddFlag(NdpHeader::PULL);
     
     Ptr<Packet> packet = Create<Packet>(0);
     packet->AddHeader(header);
     
     // Always send PULL – accepted (receiver) sockets have no m_paths but still
     // need to send control packets back to the sender.
     m_ndp->Send(packet, m_localAddress, m_remoteAddress, m_localPort, m_remotePort);
 }
 
 uint8_t
 NdpSocket::SelectPath(uint8_t avoidPath)
 {
     if (m_paths.empty())
     {
         return 0;
     }
     
     // Find next active path that is not avoidPath
     for (size_t i = 0; i < m_pathOrder.size(); i++)
     {
         uint8_t pathId = m_pathOrder[m_pathIndex];
         m_pathIndex = (m_pathIndex + 1) % m_pathOrder.size();
         
         // Re-permute after going through all paths
         if (m_pathIndex == 0)
         {
             Ptr<UniformRandomVariable> rand = CreateObject<UniformRandomVariable>();
             for (size_t j = m_pathOrder.size() - 1; j > 0; j--)
             {
                 size_t k = rand->GetInteger(0, j);
                 std::swap(m_pathOrder[j], m_pathOrder[k]);
             }
         }
         
         if (m_pathScores[pathId].active && pathId != avoidPath)
         {
             return pathId;
         }
     }
     
     // All paths inactive or avoided, use first path
     return m_pathOrder[0];
 }
 
 void
 NdpSocket::UpdatePathHealth(uint8_t pathId, bool isAck)
 {
     if (m_pathScores.find(pathId) == m_pathScores.end())
     {
         return;
     }
     
     if (isAck)
     {
         m_pathScores[pathId].acks++;
     }
     else
     {
         m_pathScores[pathId].nacks++;
     }
     
     // Periodically check path health
     CheckPathHealth();
 }
 
 void
 NdpSocket::CheckPathHealth()
 {
     // Check if any path has significantly higher NACK ratio
     double avgNackRatio = 0.0;
     uint32_t activePaths = 0;
     
     for (auto& pair : m_pathScores)
     {
         if (pair.second.active)
         {
             avgNackRatio += pair.second.GetNackRatio();
             activePaths++;
         }
     }
     
     if (activePaths > 0)
     {
         avgNackRatio /= activePaths;
     }
     
    // Deactivate paths with NACK ratio > 2x average
    bool hasDeactivated = false;
    for (auto& pair : m_pathScores)
    {
        if (pair.second.active && pair.second.GetNackRatio() > 2.0 * avgNackRatio &&
            pair.second.GetNackRatio() > 0.1)
        {
            pair.second.active = false;
            pair.second.lastProbe = Simulator::Now();  // Record when path was deactivated
            hasDeactivated = true;
            NS_LOG_INFO("Deactivated path " << (int)pair.first
                                            << " due to high NACK ratio: "
                                            << pair.second.GetNackRatio());
        }
    }
    
    // Schedule periodic probing of inactive paths if not already scheduled
    if (hasDeactivated && !m_pathProbeEvent.IsRunning())
    {
        m_pathProbeEvent = Simulator::Schedule(m_pathProbeInterval, 
                                               &NdpSocket::ProbeInactivePaths, 
                                               this);
        NS_LOG_INFO("Started path probing mechanism (interval: " 
                    << m_pathProbeInterval.GetSeconds() << "s)");
    }
}
 
// SendPulls() removed: pull pacing is now handled globally by NdpL4Protocol::SendGlobalPulls().
 
void
NdpSocket::CompleteConnection()
{
    m_state = ESTABLISHED;
    NotifyConnectionSucceeded();
}

// ============================================================
// Receiver socket initialisation (called by NdpL4Protocol for each new flow)
// ============================================================
void
NdpSocket::SetupAsReceiver(Ipv4Address localAddr,
                           uint16_t    localPort,
                           Ipv4Address remoteAddr,
                           uint16_t    remotePort,
                           uint64_t    connId,
                           uint32_t    firstSeq)
{
    NS_LOG_FUNCTION(this << localAddr << localPort << remoteAddr << remotePort
                         << connId << firstSeq);
    m_localAddress  = localAddr;
    m_localPort     = localPort;
    m_remoteAddress = remoteAddr;
    m_remotePort    = remotePort;
    m_connectionId  = connId;
    m_firstSeq      = firstSeq;
    m_expectedSeq   = firstSeq;
    m_pullSeq       = 0;
    m_state         = ESTABLISHED;
    NotifyConnectionSucceeded();
    NS_LOG_INFO("Receiver socket port=" << localPort
                << " established for connId=0x" << std::hex << connId << std::dec);
}

// ============================================================
// Called by NdpL4Protocol::SendGlobalPulls() to send one PULL for this flow
// ============================================================
void
NdpSocket::SendOnePull()
{
    NS_LOG_FUNCTION(this);
    if (m_node == nullptr || m_ndp == nullptr)
    {
        NS_LOG_WARN("SendOnePull: socket already disposed, skipping.");
        return;
    }

    m_pullSeq++;

    NS_LOG_DEBUG("SendOnePull: connId=0x" << std::hex << m_connectionId << std::dec
                 << " pullSeq=" << m_pullSeq);

    SendPull(m_pullSeq);
}

// ============================================================
// RTO 机制 —— NDP 论文：RTO 是最后防线，1ms 无任何反馈才触发
// ============================================================
void
NdpSocket::ScheduleRto(uint32_t seq)
{
    // Cancel existing timer for this seq if any
    auto it = m_rtoTimers.find(seq);
    if (it != m_rtoTimers.end())
    {
        it->second.Cancel();
        m_rtoTimers.erase(it);
    }
    
    // Schedule RTO per NDP spec, with ±25% random jitter.
    // m_rtoMs is configurable via NdpSocket::RtoMs attribute (default 1.0ms).
    //
    // Without jitter, all 301 flows starting at the same time will have
    // perfectly synchronized RTO timers (all fire at t=0.1s + rto, etc.).
    // Synchronized RTOs inject 301 packets simultaneously → switch sees a
    // 301-packet burst → 97% trimmed → cascading trim cycles.
    //
    // Jitter desynchronizes timers so at most a few flows fire per ~µs window.
    double jitterFrac = (m_rtoJitter->GetValue() - 0.5) * 0.5; // ±25% jitter
    double rtoMs      = m_rtoMs * (1.0 + jitterFrac);            // [0.75×rto, 1.25×rto]
    EventId rtoEvent  = Simulator::Schedule(Seconds(rtoMs * 1e-3),
                                            &NdpSocket::RtoExpired,
                                            this,
                                            seq);
    m_rtoTimers[seq] = rtoEvent;

    NS_LOG_DEBUG("RTO scheduled: seq=" << seq << " timeout=" << rtoMs << "ms");
}

void
NdpSocket::CancelRto(uint32_t seq)
{
    auto it = m_rtoTimers.find(seq);
    if (it != m_rtoTimers.end())
    {
        it->second.Cancel();
        m_rtoTimers.erase(it);
        NS_LOG_DEBUG("RTO cancelled: seq=" << seq);
    }
}

void
NdpSocket::RtoExpired(uint32_t seq)
{
    NS_LOG_FUNCTION(this << seq);
    
    // Guard: socket may have been disposed
    if (m_node == nullptr)
    {
        return;
    }
    
    uint32_t nodeId = m_node->GetId();
    Time now = Simulator::Now();
    
    // Remove from timer map (already fired)
    m_rtoTimers.erase(seq);

    // Check if the packet is still unacknowledged (still in txBuffer / Outstanding Table)
    auto txIt = m_txBuffer.find(seq);
    if (txIt == m_txBuffer.end())
    {
        NS_LOG_DEBUG("RTO fired but seq=" << seq << " already ACK'd, ignoring");
        return;
    }

    // ── Case A: packet already in RTX queue (waiting for PULL) ───────────────
    //
    // The sender already received a NACK/RTS (or a prior RTO fired Case B) and
    // put the packet in rtxQueue.  It will be retransmitted the moment the next
    // PULL credit arrives; SendDataPacket() will then call ScheduleRto(seq) to
    // start a fresh 1ms window for that retransmission.
    //
    // Per NDP spec: RTO is a last-resort mechanism, not a heartbeat/watchdog.
    // Do NOT re-arm here.
    // ──────────────────────────────────────────────────────────────────────────
    bool alreadyInRtx = false;
    for (const auto& rtxSeq : m_rtxQueue)
    {
        if (rtxSeq == seq) { alreadyInRtx = true; break; }
    }
    if (alreadyInRtx)
    {
        // ── Case A: packet in RTX queue, waiting for a PULL credit ──────────────
        //
        // The packet was queued for retransmission (via NACK) but no PULL has
        // arrived in 1ms.  The PULL chain may be broken (e.g., all flows have
        // stalled simultaneously so the receiver stops generating PULLs).
        //
        // Recovery: send the packet DIRECTLY (bypassing the PULL queue) to
        // inject a packet into the network.  If it's trimmed, a trim-header
        // reaches the receiver → receiver issues a fresh PULL → chain restarts.
        // If it's delivered (ACK), the flow makes forward progress.
        //
        // To avoid the old "watchdog fires millions of times" problem, we
        // re-arm with a LONGER interval (10ms) so this fires at most ~90 times
        // per flow over a 0.9s simulation.
        g_sock_rto_watchdog++;
        m_stats->rtoFires++;
        auto txItA = m_txBuffer.find(seq);
        if (txItA != m_txBuffer.end())
        {
            uint8_t avoidPathA = txItA->second.pathId;
            NS_LOG_DEBUG("RTO Case A watchdog: seq=" << seq
                         << " in RTX queue – direct retransmit to restart PULL chain");
            SendDataPacket(seq, true /*isRetx*/, avoidPathA);
        }
        // Re-arm with 10ms watchdog (longer than 1ms to limit event rate)
        double jitterFrac = (m_rtoJitter->GetValue() - 0.5) * 0.5;
        double watchdogMs = 10.0 * (1.0 + jitterFrac);
        auto watchdogEvent = Simulator::Schedule(Seconds(watchdogMs * 1e-3),
                                                 &NdpSocket::RtoExpired, this, seq);
        m_rtoTimers[seq] = watchdogEvent;
        return;
    }

    // ── Case B: packet NOT in RTX queue – true packet loss ───────────────────
    //
    // No ACK, no NACK, no PULL has arrived for this sequence number within 1ms.
    // This means either:
    //   (a) the original data packet AND its trim-header were both physically lost, OR
    //   (b) the PULL chain has stalled (all 227+ flows deadlocked: no PULL arrives
    //       because the receiver stops getting data, which stops generating PULLs).
    //
    // ✅ FIX (deadlock recovery): Send the packet DIRECTLY (without waiting for a
    // PULL credit).  This "kick-starts" the PULL chain:
    //   • If the direct retransmission is trimmed at the switch →
    //       trim-header reaches receiver → receiver generates a PULL →
    //       PULL chain restarts for this flow AND kicks other stalled flows.
    //   • If the direct retransmission is ACKed →
    //       flow makes forward progress; next PULL-driven packet follows.
    //   • If the direct retransmission is also lost →
    //       RTO fires again in 1ms and we retry.
    //
    // We do NOT add the seq to m_rtxQueue here.  The response (ACK or NACK)
    // will enqueue it via ProcessNack() if another retransmission is needed.
    // We DO re-arm the RTO so that a second true-loss can be detected.
    //
    // Limit retries to prevent livelock when a path is permanently broken.
    // ──────────────────────────────────────────────────────────────────────────
    static constexpr uint32_t MAX_RTO_RETRIES = 16; // 16 × 1ms = 16ms last-resort window
    uint32_t retries = ++txIt->second.rtoRetryCount;
    g_sock_rto_trueloss++;
    m_stats->rtoFires++;
    m_stats->rtoTrueLoss++;
    if (retries > MAX_RTO_RETRIES)
    {
        g_sock_rto_gaveup++;
        NS_LOG_ERROR("RTO seq=" << seq << " exceeded " << MAX_RTO_RETRIES
                     << " true-loss retries – dropping from Outstanding Table");
        m_txBuffer.erase(txIt);
        return;
    }

    // Direct retransmit (bypasses PULL queue) to restart the PULL chain.
    uint8_t avoidPath = txIt->second.pathId;
    NS_LOG_WARN("RTO true-loss: seq=" << seq << " retry " << retries
                << "/" << MAX_RTO_RETRIES << " → direct retransmit (kick-start PULL chain)");
    SendDataPacket(seq, true /*isRetx*/, avoidPath);

    // Re-arm RTO: will be cancelled when ACK/NACK arrives for this seq.
    ScheduleRto(seq);
}

void
NdpSocket::ProbeInactivePaths()
{
    NS_LOG_FUNCTION(this);
    
    // Guard: socket may have been disposed
    if (m_node == nullptr || m_ndp == nullptr)
    {
        return;
    }
    
    Time now = Simulator::Now();
    bool hasInactivePaths = false;
    
    // Iterate through all paths and probe inactive ones
    for (auto& pair : m_pathScores)
    {
        uint8_t pathId = pair.first;
        NdpPathScore& score = pair.second;
        
        if (!score.active)
        {
            hasInactivePaths = true;
            
            // Check if enough time has passed since last probe
            if ((now - score.lastProbe) >= m_pathProbeInterval)
            {
                // Send a probe packet on this path
                SendProbePacket(pathId);
                score.lastProbe = now;
                
                NS_LOG_INFO("Probing inactive path " << (int)pathId 
                            << " at time " << now.GetSeconds() << "s");
            }
        }
    }
    
    // Schedule next probe event if there are still inactive paths
    if (hasInactivePaths)
    {
        m_pathProbeEvent = Simulator::Schedule(m_pathProbeInterval, 
                                               &NdpSocket::ProbeInactivePaths, 
                                               this);
    }
}

void
NdpSocket::SendProbePacket(uint8_t pathId)
{
    NS_LOG_FUNCTION(this << (int)pathId);
    
    // Check if path exists
    if (pathId >= m_paths.size())
    {
        NS_LOG_WARN("Invalid path ID " << (int)pathId << " for probing");
        return;
    }
    
    // Create a small probe packet (empty payload)
    Ptr<Packet> probePacket = Create<Packet>(64);  // 64-byte probe packet
    
    // Create NDP header with a special sequence number for probe packets
    NdpHeader header;
    header.SetConnectionId(m_connectionId);
    header.SetSequence(0xFFFFFFFF);  // Special sequence for probe packets
    header.SetPathId(pathId);
    header.SetFlags(NdpHeader::NONE);  // No flags set for probe packets
    
    probePacket->AddHeader(header);
    
    // Send the probe packet on the specific path
    Ipv4Address destIp = m_paths[pathId];
    m_ndp->Send(probePacket, m_localAddress, destIp, m_localPort, m_remotePort);
    
    NS_LOG_DEBUG("Sent probe packet on path " << (int)pathId 
                 << " to " << destIp);
    
    // Note: Path will be reactivated when we receive an ACK/response for this probe
    // This happens in UpdatePathHealth() when a successful ACK is received
    // For simplicity, we can also just reactivate the path after a timeout
    // or when we receive any packet from that path
    
    // Simple reactivation: reset the path after sending probe
    // In a real implementation, you'd wait for a response
    m_pathScores[pathId].acks = 0;
    m_pathScores[pathId].nacks = 0;
    m_pathScores[pathId].losses = 0;
    m_pathScores[pathId].active = true;  // Optimistically reactivate
    
    NS_LOG_INFO("Reactivated path " << (int)pathId << " after probing");
}

} // namespace ns3
 