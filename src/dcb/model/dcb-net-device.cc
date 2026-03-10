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

 #include "dcb-net-device.h"

 #include "dcb-channel.h"
 
 #include "ns3/boolean.h"
 #include "ns3/error-model.h"
 #include "ns3/ethernet-header.h"
 #include "ns3/ipv4-header.h"
 #include "ns3/log-macros-enabled.h"
 #include "ns3/pointer.h"
 #include "ns3/queue.h"
 #include "ns3/simulator.h"
#include "ns3/traffic-control-layer.h"
 #include "ns3/type-id.h"
 
 #include <stdio.h>
#include <map>
#include <vector>
#include <algorithm>
#include <iomanip>
#include <sstream>
 
 namespace ns3
 {
 
 NS_LOG_COMPONENT_DEFINE("DcbNetDevice");
 
 NS_OBJECT_ENSURE_REGISTERED(DcbNetDevice);
 
 TypeId
 DcbNetDevice::GetTypeId(void)
 {
     static TypeId tid =
         TypeId("ns3::DcbNetDevice")
             .SetParent<NetDevice>()
             .SetGroupName("Dcb")
             .AddConstructor<DcbNetDevice>()
             .AddAttribute("Mtu",
                           "The MAC-level Maximum Transmission Unit",
                           UintegerValue(DEFAULT_MTU),
                           MakeUintegerAccessor(&DcbNetDevice::SetMtu, &DcbNetDevice::GetMtu),
                           MakeUintegerChecker<uint16_t>())
             .AddAttribute("Address",
                           "The MAC address of this device.",
                           Mac48AddressValue(Mac48Address("ff:ff:ff:ff:ff:ff")),
                           MakeMac48AddressAccessor(&DcbNetDevice::m_address),
                           MakeMac48AddressChecker())
             .AddAttribute("DataRate",
                           "The default data rate for point to point links",
                           DataRateValue(DataRate("100Gb/s")),
                           MakeDataRateAccessor(&DcbNetDevice::m_bps),
                           MakeDataRateChecker())
             .AddAttribute("FcEnabled",
                           "Enable flow control functions",
                           BooleanValue(false),
                           MakeBooleanAccessor(&DcbNetDevice::m_fcEnabled),
                           MakeBooleanChecker())
             .AddAttribute("ReceiveErrorModel",
                           "The receiver error model used to simulate packet loss",
                           PointerValue(),
                           MakePointerAccessor(&DcbNetDevice::m_receiveErrorModel),
                           MakePointerChecker<ErrorModel>())
             .AddAttribute("InterframeGap",
                           "The time to wait between packet (frame) transmissions",
                           TimeValue(Seconds(0.0)),
                           MakeTimeAccessor(&DcbNetDevice::m_tInterframeGap),
                           MakeTimeChecker())
             //
             // Transmit queueing discipline for the device which includes its own set
             // of trace hooks.
             //
             .AddAttribute("TxQueue",
                           "A queue to use as the transmit queue in the device.",
                           PointerValue(),
                           MakePointerAccessor(&DcbNetDevice::m_queue),
                           MakePointerChecker<Queue<Packet>>())
             //
             // Trace sources at the "top" of the net device, where packets transition
             // to/from higher layers.
             //
             .AddTraceSource("MacTx",
                             "Trace source indicating a packet has arrived "
                             "for transmission by this device",
                             MakeTraceSourceAccessor(&DcbNetDevice::m_macTxTrace),
                             "ns3::Packet::TracedCallback")
             .AddTraceSource("MacTxDrop",
                             "Trace source indicating a packet has been dropped "
                             "by the device before transmission",
                             MakeTraceSourceAccessor(&DcbNetDevice::m_macTxDropTrace),
                             "ns3::Packet::TracedCallback")
             .AddTraceSource("MacRx",
                             "A packet has been received by this device, "
                             "has been passed up from the physical layer "
                             "and is being forwarded up the local protocol stack.  "
                             "This is a non-promiscuous trace,",
                             MakeTraceSourceAccessor(&DcbNetDevice::m_macRxTrace),
                             "ns3::Packet::TracedCallback")
             //
             // Trace sources at the "bottom" of the net device, where packets transition
             // to/from the channel.
             //
             .AddTraceSource("PhyTxBegin",
                             "Trace source indicating a packet has begun "
                             "transmitting over the channel",
                             MakeTraceSourceAccessor(&DcbNetDevice::m_phyTxBeginTrace),
                             "ns3::Packet::TracedCallback")
             .AddTraceSource("PhyTxBeginWithId",
                             "Trace source indicating a packet has begun "
                             "transmitting over the channel",
                             MakeTraceSourceAccessor(&DcbNetDevice::m_phyTxBeginWithIdTrace),
                             "ns3::Packet::TracedCallback")
             .AddTraceSource("PhyTxEnd",
                             "Trace source indicating a packet has been "
                             "completely transmitted over the channel",
                             MakeTraceSourceAccessor(&DcbNetDevice::m_phyTxEndTrace),
                             "ns3::Packet::TracedCallback")
             .AddTraceSource("PhyTxDrop",
                             "Trace source indicating a packet has been "
                             "dropped by the device during transmission",
                             MakeTraceSourceAccessor(&DcbNetDevice::m_phyTxDropTrace),
                             "ns3::Packet::TracedCallback")
 #if 0
           // Not currently implemented for this device
           .AddTraceSource ("PhyRxBegin",
                            "Trace source indicating a packet has begun "
                            "being received by the device",
                            MakeTraceSourceAccessor (&DcbNetDevice::m_phyRxBeginTrace),
                            "ns3::Packet::TracedCallback")
 #endif
             .AddTraceSource("PhyRxEnd",
                             "Trace source indicating a packet has been "
                             "completely received by the device",
                             MakeTraceSourceAccessor(&DcbNetDevice::m_phyRxEndTrace),
                             "ns3::Packet::TracedCallback")
             .AddTraceSource("PhyRxDrop",
                             "Trace source indicating a packet has been "
                             "dropped by the device during reception",
                             MakeTraceSourceAccessor(&DcbNetDevice::m_phyRxDropTrace),
                             "ns3::Packet::TracedCallback")
             //
             // Trace sources designed to simulate a packet sniffer facility (tcpdump).
             // Note that there is really no difference between promiscuous and
             // non-promiscuous traces in a point-to-point link.
             //
             .AddTraceSource("Sniffer",
                             "Trace source simulating a non-promiscuous packet sniffer "
                             "attached to the device",
                             MakeTraceSourceAccessor(&DcbNetDevice::m_snifferTrace),
                             "ns3::Packet::TracedCallback");
 
     return tid;
 }
 
 DcbNetDevice::DcbNetDevice()
     : m_node(nullptr),
       m_ifIndex(0x7fffffff),
       m_linkUp(false),
       m_mtu(DEFAULT_MTU),
       m_tInterframeGap(),
       m_channel(0),
       m_queue(nullptr),
       m_fcEnabled(false),
       m_queueDisc(nullptr),
       m_receiveErrorModel(nullptr),
       m_txMachineState(READY),
       m_currentPkt(nullptr)
 {
     NS_LOG_FUNCTION(this);
 }
 
 DcbNetDevice::~DcbNetDevice()
 {
     NS_LOG_FUNCTION(this);
 }
 
 void
 DcbNetDevice::DoDispose()
 {
     m_node = 0;
     m_channel = 0;
     m_receiveErrorModel = 0;
     m_currentPkt = 0;
     m_queue = 0;
     NetDevice::DoDispose();
 }
 
 void
 PrintRawPacket(Ptr<Packet> p)
 {
     uint32_t sz = p->GetSerializedSize();
     uint8_t* buffer = new uint8_t[sz];
     p->Serialize(buffer, sz);
     printf("Raw packet:");
     for (uint32_t i = 0; i < sz; i++)
     {
         if (i % 16 == 0)
         {
             printf("\n");
         }
         printf("%02x ", buffer[i]);
     }
 }
 
// Global counter for NDP packets received at any DcbNetDevice
uint64_t g_nic_ndp_rx = 0;
// Global counter for NDP packets transmitted by any DcbNetDevice
uint64_t g_nic_ndp_tx = 0;
// Global counter for NDP packets dropped at DcbNetDevice m_queue
uint64_t g_nic_ndp_drop = 0;  // was static, now extern-visible for diagnostics

// ══════════════════════════════════════════════════════════════════════════════
// Per-device link utilization tracking
// ══════════════════════════════════════════════════════════════════════════════
struct LinkUtilRecord {
    uint64_t txBytes = 0;       ///< total bytes transmitted on this device
    uint64_t txPkts  = 0;       ///< total packets transmitted
    uint64_t linkRateBps = 0;   ///< link rate in bits per second
    uint32_t nodeId = 0;        ///< owning node ID
    uint32_t ifIndex = 0;       ///< interface index on that node
    uint32_t peerNodeId = UINT32_MAX;  ///< peer node ID (other end of the link)
    Time     firstTx;           ///< time of first transmission
    Time     lastTx;            ///< time of last transmission
    bool     hasTx = false;     ///< whether any packet was transmitted
};

/// key = DcbNetDevice raw pointer (stable for simulation lifetime)
static std::map<const DcbNetDevice*, LinkUtilRecord> g_linkUtil;

void PrintLinkUtilizationStats()
{
    if (g_linkUtil.empty()) return;

    // Find global first/last time across ALL devices for overall utilization
    Time globalFirst = Seconds(1e9), globalLast = Seconds(0);
    for (auto& [dev, rec] : g_linkUtil)
    {
        if (!rec.hasTx) continue;
        if (rec.firstTx < globalFirst) globalFirst = rec.firstTx;
        if (rec.lastTx > globalLast) globalLast = rec.lastTx;
    }
    double globalDuration = (globalLast - globalFirst).GetSeconds();
    if (globalDuration <= 0) globalDuration = 1e-9;

    // Build utilization entries
    struct UtilEntry {
        uint32_t srcNode;
        uint32_t dstNode;
        uint32_t ifIndex;
        uint64_t txBytes;
        uint64_t txPkts;
        uint64_t linkRateBps;
        double   util;         // over global duration
        double   throughputGbps;
    };
    std::vector<UtilEntry> entries;
    for (auto& [dev, rec] : g_linkUtil)
    {
        if (!rec.hasTx) continue;
        double util = (rec.linkRateBps > 0)
                      ? (double)(rec.txBytes * 8) / (globalDuration * rec.linkRateBps) * 100.0
                      : 0.0;
        double tput = (double)(rec.txBytes * 8) / globalDuration / 1e9;
        entries.push_back({rec.nodeId, rec.peerNodeId, rec.ifIndex,
                           rec.txBytes, rec.txPkts, rec.linkRateBps, util, tput});
    }
    std::sort(entries.begin(), entries.end(),
              [](const UtilEntry& a, const UtilEntry& b){ return a.util > b.util; });

    // Summary statistics
    double maxUtil = 0, minUtil = 1e9, sumUtil = 0;
    for (auto& e : entries) {
        if (e.util > maxUtil) maxUtil = e.util;
        if (e.util < minUtil) minUtil = e.util;
        sumUtil += e.util;
    }
    double avgUtil = entries.empty() ? 0 : sumUtil / entries.size();

    std::cout
        << "╔════════════════════════════════════════════════════════════════════════════╗\n"
        << "║                   Per-Link Utilization Statistics                         ║\n"
        << "╠════════════════════════════════════════════════════════════════════════════╣\n"
        << "║  Global active window: "
        << std::fixed << std::setprecision(3) << globalFirst.GetMilliSeconds()
        << " ms → " << globalLast.GetMilliSeconds() << " ms ("
        << std::setprecision(3) << globalDuration * 1000.0 << " ms)\n"
        << "║  Active links: " << entries.size() << "\n"
        << "║  Utilization: min=" << std::setprecision(1) << minUtil
        << "% avg=" << std::setprecision(1) << avgUtil
        << "% max=" << std::setprecision(1) << maxUtil << "%\n"
        << "╠════════════════════════════════════════════════════════════════════════════╣\n"
        << "║  Top-30 busiest links (direction: Src → Dst):                            ║\n"
        << "║  Src→Dst          TxBytes    TxPkts  Rate   Throughput  Utilization       ║\n"
        << "╠════════════════════════════════════════════════════════════════════════════╣\n";

    size_t printed = 0;
    for (auto& e : entries)
    {
        if (printed >= 30) break;
        // Format direction as "SrcNode → DstNode"
        std::ostringstream dir;
        dir << std::setw(5) << e.srcNode << "→" << std::setw(5);
        if (e.dstNode != UINT32_MAX)
            dir << e.dstNode;
        else
            dir << "?";

        std::cout << "║  " << dir.str()
                  << "  " << std::setw(12) << e.txBytes
                  << "  " << std::setw(8) << e.txPkts
                  << "  " << std::setw(4) << (e.linkRateBps / 1000000000ULL) << "G"
                  << "  " << std::setw(8) << std::fixed << std::setprecision(2) << e.throughputGbps << "G"
                  << "  " << std::setw(7) << std::fixed << std::setprecision(2) << e.util << "%\n";
        printed++;
    }

    // Also print bottom-5
    if (entries.size() > 35)
    {
        std::cout << "║  ... (" << (entries.size() - 35) << " more links) ...\n"
                  << "║  Bottom-5 least busy links:\n";
        for (size_t i = std::max(entries.size() - 5, (size_t)30); i < entries.size(); i++)
        {
            auto& e = entries[i];
            std::ostringstream dir;
            dir << std::setw(5) << e.srcNode << "→" << std::setw(5);
            if (e.dstNode != UINT32_MAX)
                dir << e.dstNode;
            else
                dir << "?";
            std::cout << "║  " << dir.str()
                      << "  " << std::setw(12) << e.txBytes
                      << "  " << std::setw(8) << e.txPkts
                      << "  " << std::setw(4) << (e.linkRateBps / 1000000000ULL) << "G"
                      << "  " << std::setw(8) << std::fixed << std::setprecision(2) << e.throughputGbps << "G"
                      << "  " << std::setw(7) << std::fixed << std::setprecision(2) << e.util << "%\n";
        }
    }

    // Print a per-link-type summary for incast scenarios
    // Categorize by: switch→host vs host→switch
    uint64_t swToHostBytes = 0, hostToSwBytes = 0;
    uint32_t swToHostCount = 0, hostToSwCount = 0;
    uint32_t switchNodeId = UINT32_MAX;
    // Heuristic: the node with the most tx records is the switch
    std::map<uint32_t, uint32_t> nodeRecCount;
    for (auto& e : entries) nodeRecCount[e.srcNode]++;
    uint32_t maxRecCount = 0;
    for (auto& [nid, cnt] : nodeRecCount)
    {
        if (cnt > maxRecCount) { maxRecCount = cnt; switchNodeId = nid; }
    }
    if (switchNodeId != UINT32_MAX && maxRecCount > 2)
    {
        for (auto& e : entries)
        {
            if (e.srcNode == switchNodeId) { swToHostBytes += e.txBytes; swToHostCount++; }
            else { hostToSwBytes += e.txBytes; hostToSwCount++; }
        }
        double swToHostGbps = (double)(swToHostBytes * 8) / globalDuration / 1e9;
        double hostToSwGbps = (double)(hostToSwBytes * 8) / globalDuration / 1e9;
        std::cout
            << "╠════════════════════════════════════════════════════════════════════════════╣\n"
            << "║  Aggregate by direction (Switch = node " << switchNodeId << "):\n"
            << "║    Switch→Host (" << swToHostCount << " links): "
            << std::setprecision(2) << swToHostGbps << " Gbps total"
            << " (avg " << std::setprecision(2) << swToHostGbps / std::max(swToHostCount, 1u) << " Gbps/link)\n"
            << "║    Host→Switch (" << hostToSwCount << " links): "
            << std::setprecision(2) << hostToSwGbps << " Gbps total"
            << " (avg " << std::setprecision(2) << hostToSwGbps / std::max(hostToSwCount, 1u) << " Gbps/link)\n";
    }

    std::cout << "╚════════════════════════════════════════════════════════════════════════════╝\n";
}

 void
 DcbNetDevice::Receive(Ptr<Packet> packet)
 {
     NS_LOG_FUNCTION(this << packet);

     // Quickly peek EtherType to count NDP packets (protocol 253 = 0xFD over IPv4 = 0x0800)
     // We only need to track arrival, not full decoding
     {
         EthernetHeader ethHdr;
         Ptr<Packet> tmp = packet->Copy();
         if (tmp->PeekHeader(ethHdr) > 0 && ethHdr.GetLengthType() == 0x0800)
         {
             // IPv4 packet - peek the IP header protocol field
             tmp->RemoveHeader(ethHdr);
             Ipv4Header ipHdr;
             if (tmp->PeekHeader(ipHdr) > 0 && ipHdr.GetProtocol() == 253)
             {
                 g_nic_ndp_rx++;
             }
         }
     }

     if (m_receiveErrorModel && m_receiveErrorModel->IsCorrupt(packet))
     {
         //
         // If we have an error model and it indicates that it is time to lose a
         // corrupted packet, don't forward this packet up, let it go.
         //
         m_phyRxDropTrace(packet);
     }
     else
     {
         // Hit the trace hooks.  All of these hooks are in the same place in this
         // device because it is so simple, but this is not usually the case in
         // more complicated devices.
         //
         m_snifferTrace(packet);
         m_phyRxEndTrace(packet);
 
         Ptr<Packet> originalPacket = packet->Copy();
 
         EthernetHeader ethHeader;
         packet->RemoveHeader(ethHeader);
         uint16_t protocol = ethHeader.GetLengthType();
 
         //
         // Trace sinks will expect complete packets, not packets without some of the
         // headers.
         //
 
         m_macRxTrace(originalPacket);
         m_rxCallback(this,
                      packet,
                      protocol,
                      GetRemote()); // calling Node::NonPromiscReceiveFromDevice
     }
 }
 
 bool
 DcbNetDevice::Send(Ptr<Packet> packet, const Address& dest, uint16_t protocolNumber)
 {
     NS_LOG_FUNCTION(this << packet << dest << protocolNumber);
     NS_LOG_LOGIC("p=" << packet << ", dest=" << &dest);
     NS_LOG_LOGIC("UID is " << packet->GetUid());
 
     //
     // If IsLinkUp() is false it means there is no channel to send any packet
     // over so we just hit the drop trace on the packet and return an error.
     //
     if (IsLinkUp() == false)
     {
         m_macTxDropTrace(packet);
         return false;
     }
 
     //
     // Stick a point to point protocol header on the packet in preparation for
     // shoving it out the door.
     //
 
     AddEthernetHeader(packet, protocolNumber);

     m_macTxTrace(packet);

     //
     // We should enqueue and dequeue the packet to hit the tracing hooks.
     //
     if (m_queue->Enqueue(packet))
     {
         // Count NDP transmissions only when successfully enqueued (will actually be sent)
         if (protocolNumber == 0x0800)
         {
             // peek inside the ethernet frame for IPv4 protocol field
             Ptr<Packet> pktCopy = packet->Copy();
             EthernetHeader ethHdr;
             pktCopy->RemoveHeader(ethHdr);
             Ipv4Header ipHdr;
             if (pktCopy->PeekHeader(ipHdr) > 0 && ipHdr.GetProtocol() == 253)
             {
                 g_nic_ndp_tx++;
             }
         }
         //
         // If the channel is ready for transition we send the packet right now
         //
         if (m_txMachineState == READY)
         {
             packet = m_queue->Dequeue();
             // m_promiscSnifferTrace (packet);
             bool ret = TransmitStart(packet);
             return ret;
         }
         return true;
     }
 
     // Enqueue may fail (overflow): count NDP drops here
     {
         EthernetHeader ethHdr;
         Ptr<Packet> pktCopy2 = packet->Copy();
         pktCopy2->RemoveHeader(ethHdr);
         Ipv4Header ipHdr2;
         if (pktCopy2->PeekHeader(ipHdr2) > 0 && ipHdr2.GetProtocol() == 253)
         {
             g_nic_ndp_drop++;
         }
     }

     m_macTxDropTrace(packet);
     return false;
 }
 
 bool
 DcbNetDevice::TransmitStart(Ptr<Packet> packet)
 {
     NS_LOG_FUNCTION(this << packet);
     NS_LOG_LOGIC("UID is " << packet->GetUid() << ")");
 
     //
     // This function is called to start the process of transmitting a packet.
     // We need t tell the channel that we've started wiggling the wire and
     // schedule an event that will be executed when the transmission is complete.
     //
 
    NS_ASSERT_MSG(m_txMachineState == READY, "Must be READY to transmit");

    m_txMachineState = BUSY;
    m_currentPkt = packet;
    m_phyTxBeginTrace(m_currentPkt);
    m_phyTxBeginWithIdTrace(m_currentPkt, GetNodeAndPortId());
    m_snifferTrace(packet);

    // ── Per-link utilization tracking ─────────────────────────────────────
    {
        auto& rec = g_linkUtil[this];
        rec.txBytes += packet->GetSize();
        rec.txPkts++;
        rec.linkRateBps = m_bps.GetBitRate();
        rec.nodeId  = m_node ? m_node->GetId() : 0;
        rec.ifIndex = m_ifIndex;
        Time now = Simulator::Now();
        if (!rec.hasTx)
        {
            rec.firstTx = now;
            rec.hasTx = true;
            // Determine peer node via channel
            if (m_channel)
            {
                // m_channel has two devices: link[0].m_src and link[1].m_src
                // Our peer is the other one
                Ptr<NetDevice> dev0 = m_channel->GetDevice(0);
                Ptr<NetDevice> dev1 = m_channel->GetDevice(1);
                Ptr<NetDevice> peer = (dev0.operator->() == this) ? dev1 : dev0;
                if (peer && peer->GetNode())
                {
                    rec.peerNodeId = peer->GetNode()->GetId();
                }
            }
        }
        rec.lastTx = now;
    }

    Time txTime = m_bps.CalculateBytesTxTime(packet->GetSize());
    Time txCompleteTime = txTime + m_tInterframeGap;
 
     NS_LOG_LOGIC("Schedule TransmitCompleteEvent in " << txCompleteTime.As(Time::S));
     Simulator::Schedule(txCompleteTime, &DcbNetDevice::TransmitComplete, this);
 
     bool result = m_channel->TransmitStart(packet, this, txTime);
     if (result == false)
     {
         m_phyTxDropTrace(packet);
     }
     return result;
 }
 
 void
 DcbNetDevice::TransmitComplete(void)
 {
     NS_LOG_FUNCTION(this);
 
     //
     // This function is called to when we're all done transmitting a packet.
     // We try and pull another packet off of the transmit queue.  If the queue
     // is empty, we are done, otherwise we need to start transmitting the
     // next packet.
     //
     NS_ASSERT_MSG(m_txMachineState == BUSY, "Must be BUSY if transmitting");
     m_txMachineState = READY;
 
     NS_ASSERT_MSG(m_currentPkt != nullptr, "DcbNetDevice::TransmitComplete(): m_currentPkt zero");
 
     m_phyTxEndTrace(m_currentPkt);
     m_currentPkt = 0;
 
     Ptr<Packet> p = m_queue->Dequeue();
     if (m_fcEnabled)
     {
         // We let the egress buffer, which is a PausableQueueDisc, to pop one packet at
         // a time and send out through this device. As a result, at most one data packet
         // should be in the m_queue.
         // If device queue has packets left, they must be control frames. Send them
         // immedidately.
         if (p)
         {
             TransmitStart(p);
         }
 
         /**
          * If the device queue is empty, make queueDisc to send one packet down.
          * Should not do this procedure every time when a packet is sent out, given
          * that the FC Frame is sent out through this device as well.
          */
         if (m_queue->GetCurrentSize() == QueueSize("0p"))
         {
            // DoDequeue trigger: "Transmission complete → RunEnd() → Run() → DoDequeue()"
            // This is the ONLY correct trigger besides the idle→busy first-send case.
            // (Not timer-based, not poll-based, not immediately-after-enqueue)
            //
            // m_queueDisc holds PausableQueueDisc (RoCEv2). For NDP, the root qdisc is
            // NdpSwitchQueue which is NOT a PausableQueueDisc, so m_queueDisc is null.
            // Fall back to querying TrafficControlLayer for the actual root qdisc.
            Ptr<QueueDisc> activeQdisc = m_queueDisc;
            if (!activeQdisc)
            {
                Ptr<Node> node = GetNode();
                if (node)
                {
                    Ptr<TrafficControlLayer> tc = node->GetObject<TrafficControlLayer>();
                    if (tc)
                    {
                        activeQdisc = tc->GetRootQueueDiscOnDevice(this);
                    }
                }
            }

            if (activeQdisc)
            {
                // Step 1: release m_running so Run() can enter
                activeQdisc->RunEnd();
                // Step 2: dequeue next packet → DoDequeue() (transmission-complete trigger)
                activeQdisc->Run();
             }
             else
             {
                NS_LOG_DEBUG("TransmitComplete: no queueDisc found for this device");
             }
         }
         else if (m_queue->GetCurrentSize() >= QueueSize("2p"))
         {
             NS_LOG_DEBUG("Device queue is not empty after transmitting a packet. Current size: "
                          << m_queue->GetCurrentSize() << "");
         }
     }
     else
     {
         if (p == nullptr)
         {
             NS_LOG_LOGIC("No pending packets in device queue after tx complete");
             return;
         }
         //
         // Got another packet off of the queue, so start the transmit process again.
         //
         TransmitStart(p);
     }
 }
 
 Address
 DcbNetDevice::GetRemote(void) const
 {
     NS_LOG_FUNCTION(this);
     NS_ASSERT(m_channel->GetNDevices() == 2);
     for (std::size_t i = 0; i < m_channel->GetNDevices(); ++i)
     {
         Ptr<NetDevice> tmp = m_channel->GetDevice(i);
         if (tmp != this)
         {
             return tmp->GetAddress();
         }
     }
     NS_ASSERT(false);
     // quiet compiler.
     return Address();
 }
 
 void
 DcbNetDevice::SetDataRate(DataRate bps)
 {
     NS_LOG_FUNCTION(this);
     m_bps = bps;
 }
 
 DataRate
 DcbNetDevice::GetDataRate() const
 {
     NS_LOG_FUNCTION(this);
     return m_bps;
 }
 
 void
 DcbNetDevice::SetInterframeGap(Time t)
 {
     NS_LOG_FUNCTION(this << t.As(Time::S));
     m_tInterframeGap = t;
 }
 
 void
 DcbNetDevice::AddEthernetHeader(Ptr<Packet> p, uint16_t protocolNumber)
 {
     NS_LOG_FUNCTION(this << p << protocolNumber);
     EthernetHeader eth;
     eth.SetSource(Mac48Address::ConvertFrom(GetAddress()));
     eth.SetDestination(Mac48Address::ConvertFrom(GetRemote()));
     eth.SetLengthType(protocolNumber);
     p->AddHeader(eth);
 }
 
 Ptr<Node>
 DcbNetDevice::GetNode(void) const
 {
     return m_node;
 }
 
 void
 DcbNetDevice::SetNode(Ptr<Node> node)
 {
     NS_LOG_FUNCTION(this);
     m_node = node;
 }
 
 void
 DcbNetDevice::SetAddress(Address address)
 {
     m_address = Mac48Address::ConvertFrom(address);
 }
 
 Address
 DcbNetDevice::GetAddress(void) const
 {
     return m_address;
 }
 
 bool
 DcbNetDevice::Attach(Ptr<DcbChannel> ch)
 {
     NS_LOG_FUNCTION(this << &ch);
 
     m_channel = ch;
 
     m_channel->Attach(this);
 
     //
     // This device is up whenever it is attached to a channel.  A better plan
     // would be to have the link come up when both devices are attached, but this
     // is not done for now.
     //
     NotifyLinkUp();
     return true;
 }
 
 void
 DcbNetDevice::SetQueue(Ptr<Queue<Packet>> q)
 {
     NS_LOG_FUNCTION(this << q);
     m_queue = q;
 }
 
 bool
 DcbNetDevice::IsLinkUp(void) const
 {
     NS_LOG_FUNCTION(this);
     return m_linkUp;
 }
 
 void
 DcbNetDevice::NotifyLinkUp()
 {
     NS_LOG_FUNCTION(this);
     m_linkUp = true;
 }
 
 bool
 DcbNetDevice::NeedsArp(void) const
 {
     NS_LOG_FUNCTION(this);
     return false;
 }
 
 void
 DcbNetDevice::SetReceiveCallback(NetDevice::ReceiveCallback cb)
 {
     m_rxCallback = cb;
 }
 
 bool
 DcbNetDevice::SetMtu(uint16_t mtu)
 {
     NS_LOG_FUNCTION(this << mtu);
     m_mtu = mtu;
     return true;
 }
 
 uint16_t
 DcbNetDevice::GetMtu(void) const
 {
     NS_LOG_FUNCTION(this);
     return m_mtu;
 }
 
 void
 DcbNetDevice::SetIfIndex(const uint32_t index)
 {
     NS_LOG_FUNCTION(this);
     m_ifIndex = index;
 }
 
 uint32_t
 DcbNetDevice::GetIfIndex(void) const
 {
     NS_ASSERT_MSG(m_ifIndex < 0x7fffffff, "DcbNetDevice index not set");
     return m_ifIndex;
 }
 
 Ptr<Channel>
 DcbNetDevice::GetChannel(void) const
 {
     return m_channel;
 }
 
 //
 // This is a point-to-point device, so every transmission is a broadcast to
 // all of the devices on the network.
 //
 bool
 DcbNetDevice::IsBroadcast(void) const
 {
     NS_LOG_FUNCTION(this);
     return true;
 }
 
 Address
 DcbNetDevice::GetBroadcast(void) const
 {
     NS_LOG_FUNCTION(this);
     return Mac48Address("ff:ff:ff:ff:ff:ff");
 }
 
 bool
 DcbNetDevice::IsMulticast(void) const
 {
     NS_LOG_FUNCTION(this);
     return true;
 }
 
 Address
 DcbNetDevice::GetMulticast(Ipv4Address multicastGroup) const
 {
     NS_LOG_FUNCTION(this);
     return Mac48Address("01:00:5e:00:00:00");
 }
 
 Address
 DcbNetDevice::GetMulticast(Ipv6Address addr) const
 {
     NS_LOG_FUNCTION(this << addr);
     return Mac48Address("33:33:00:00:00:00");
 }
 
 bool
 DcbNetDevice::IsPointToPoint(void) const
 {
     NS_LOG_FUNCTION(this);
     return true;
 }
 
 bool
 DcbNetDevice::IsBridge(void) const
 {
     NS_LOG_FUNCTION(this);
     return false;
 }
 
 void
 DcbNetDevice::AddLinkChangeCallback(Callback<void> callback)
 {
     NS_LOG_FUNCTION(this);
     NS_FATAL_ERROR("DcbNetDevice::AddLinkChangeCallback () not implemented");
 }
 
 void
 DcbNetDevice::SetPromiscReceiveCallback(PromiscReceiveCallback cb)
 {
     NS_LOG_FUNCTION(this);
     NS_FATAL_ERROR("DcbNetDevice::SetPromiscReceiveCallback () not implemented");
 }
 
 bool
 DcbNetDevice::SupportsSendFrom(void) const
 {
     NS_LOG_FUNCTION(this);
     return false;
 }
 
 void
 DcbNetDevice::SetQueueDisc(Ptr<PausableQueueDisc> queueDisc)
 {
     NS_LOG_FUNCTION(this << queueDisc);
     m_queueDisc = queueDisc;
 }
 
 Ptr<PausableQueueDisc>
 DcbNetDevice::GetQueueDisc() const
 {
     NS_LOG_FUNCTION(this);
     return m_queueDisc;
 }
 
 void
 DcbNetDevice::SetFcEnabled(bool enabled)
 {
     NS_LOG_FUNCTION(this << enabled);
     m_fcEnabled = enabled;
 }
 
 bool
 DcbNetDevice::SendFrom(Ptr<Packet> packet,
                        const Address& source,
                        const Address& dest,
                        uint16_t protocolNumber)
 {
     NS_LOG_FUNCTION(this << packet << source << dest << protocolNumber);
     return false;
 }
 
 std::pair<uint32_t, uint32_t>
 DcbNetDevice::GetNodeAndPortId() const
 {
     return std::make_pair(m_node->GetId(), m_ifIndex);
 }
 
 } // namespace ns3
 