/*
 * Copyright (c) 2024
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
 * NDP Switch Queue Implementation
 */

 #include "ndp-switch-queue.h"
 #include "switch-node.h"

 #include "../utils/ndp-header.h"
  
 #include "ns3/ipv4-header.h"
 #include <fstream>
 #include <chrono>
 #include <iomanip>
 #include "ns3/ipv4-queue-disc-item.h"
 #include "ns3/log.h"
 #include "ns3/node-list.h"
 #include "ns3/packet.h"
 #include "ns3/pointer.h"
 #include "ns3/simulator.h"
 #include "ns3/uinteger.h"
#include "ns3/global-value.h"
#include "ns3/boolean.h"
#include "ns3/string.h"
  
namespace ns3
{

NS_LOG_COMPONENT_DEFINE("NdpSwitchQueue");

// ══════════════════════════════════════════════════════════════════════════════
// Global simulation-wide counters for NDP switch queue events.
// Printed at simulation end via Simulator::ScheduleDestroy.
// ══════════════════════════════════════════════════════════════════════════════
static uint64_t g_sw_data_in    = 0;  ///< data pkts arriving at any switch
static uint64_t g_sw_ctrl_in    = 0;  ///< ctrl pkts arriving at any switch
static uint64_t g_sw_head_trim  = 0;  ///< head-trim events (arriving pkt trimmed)
static uint64_t g_sw_tail_trim  = 0;  ///< tail-trim events (tail pkt evicted)
static uint64_t g_sw_rts        = 0;  ///< Return-To-Sender events
static uint64_t g_sw_ctrl_drop  = 0;  ///< ctrl pkts dropped (HighQ full)
static uint64_t g_sw_ctrl_dequeued = 0; ///< ctrl pkts dequeued (actually sent on wire)

void PrintNdpSwitchStats()
{
    uint64_t totalTrim = g_sw_head_trim + g_sw_tail_trim;
    std::cout << "\n"
              << "╔══════════════════════════════════════════════════╗\n"
              << "║          NDP Switch Queue Statistics             ║\n"
              << "╠══════════════════════════════════════════════════╣\n"
              << "║  Data packets in        : " << std::setw(12) << g_sw_data_in    << "          ║\n"
              << "║  Ctrl packets in        : " << std::setw(12) << g_sw_ctrl_in    << "          ║\n"
              << "║  Trim (head)            : " << std::setw(12) << g_sw_head_trim  << "          ║\n"
              << "║  Trim (tail)            : " << std::setw(12) << g_sw_tail_trim  << "          ║\n"
              << "║  Trim total             : " << std::setw(12) << totalTrim        << "          ║\n"
              << "║  Trim rate              : " << std::setw(10) << std::fixed << std::setprecision(2)
              << (g_sw_data_in ? (static_cast<double>(totalTrim) * 100.0 / g_sw_data_in) : 0.0) << "%"  << "          ║\n"
              << std::defaultfloat
              << "║  RTS (return-to-sender) : " << std::setw(12) << g_sw_rts        << "          ║\n"
              << "║  Ctrl DROP (HighQ full) : " << std::setw(12) << g_sw_ctrl_drop     << "          ║\n"
              << "║  Ctrl DEQUEUED (sent)   : " << std::setw(12) << g_sw_ctrl_dequeued << "          ║\n"
              << "╚══════════════════════════════════════════════════╝\n";
}
  
  NS_OBJECT_ENSURE_REGISTERED(NdpSwitchQueue);
  
 TypeId
 NdpSwitchQueue::GetTypeId()
 {
     static TypeId tid =
         TypeId("ns3::NdpSwitchQueue")
             .SetParent<QueueDisc>()
              .SetGroupName("Dcb")
              .AddConstructor<NdpSwitchQueue>()
              .AddAttribute("LowQueueMaxPackets",
                            "Maximum number of packets in low queue (data packets)",
                            UintegerValue(8),
                            MakeUintegerAccessor(&NdpSwitchQueue::m_lowQueueMaxPackets),
                            MakeUintegerChecker<uint32_t>())
              .AddAttribute("HighQueueMaxPackets",
                            "Maximum number of packets in high queue (control packets)",
                            UintegerValue(1000),
                            MakeUintegerAccessor(&NdpSwitchQueue::m_highQueueMaxPackets),
                            MakeUintegerChecker<uint32_t>())
              .AddAttribute("HighQueueWeight",
                            "Weight for high queue in WRR scheduling",
                            UintegerValue(10),
                            MakeUintegerAccessor(&NdpSwitchQueue::m_highQueueWeight),
                            MakeUintegerChecker<uint32_t>())
              .AddAttribute("LowQueueWeight",
                            "Weight for low queue in WRR scheduling",
                            UintegerValue(1),
                            MakeUintegerAccessor(&NdpSwitchQueue::m_lowQueueWeight),
                            MakeUintegerChecker<uint32_t>())
              .AddTraceSource("NdpEnqueue",
                              "Enqueue a packet in NDP switch queue",
                              MakeTraceSourceAccessor(&NdpSwitchQueue::m_ndpTraceEnqueue),
                              "ns3::QueueDiscItem::TracedCallback")
              .AddTraceSource("NdpDequeue",
                              "Dequeue a packet from NDP switch queue",
                              MakeTraceSourceAccessor(&NdpSwitchQueue::m_ndpTraceDequeue),
                              "ns3::QueueDiscItem::TracedCallback")
              .AddTraceSource("NdpDrop",
                              "Drop a packet in NDP switch queue",
                              MakeTraceSourceAccessor(&NdpSwitchQueue::m_ndpTraceDrop),
                              "ns3::QueueDiscItem::TracedCallback")
              .AddTraceSource("Trim",
                              "Trim a packet",
                              MakeTraceSourceAccessor(&NdpSwitchQueue::m_traceTrim),
                              "ns3::QueueDiscItem::TracedCallback")
              .AddTraceSource("ReturnToSender",
                              "Return to sender",
                              MakeTraceSourceAccessor(&NdpSwitchQueue::m_traceReturnToSender),
                              "ns3::QueueDiscItem::TracedCallback");
      return tid;
  }
  
  NdpSwitchQueue::NdpSwitchQueue()
      : m_lowQueueMaxPackets(8),
        m_highQueueMaxPackets(1000),
        m_highQueueWeight(10),
        m_lowQueueWeight(1),
        m_highQueueCredits(0),
        m_lowQueueCredits(0),
        m_trimCount(0),
        m_returnToSenderCount(0),
        m_tailTrimCount(0),
        m_headTrimCount(0)
  {
      NS_LOG_FUNCTION(this);
      m_trimDecision = CreateObject<UniformRandomVariable>();
      m_trimDecision->SetAttribute("Min", DoubleValue(0.0));
      m_trimDecision->SetAttribute("Max", DoubleValue(1.0));

      // Check global config for detailed queue stats
      BooleanValue bv;
      bool detailed = false;
      if (GlobalValue::GetValueByNameFailSafe("detailedQlengthStats", bv))
      {
          detailed = bv.Get();
      }
      m_lowQueueStats.detailedQlength = detailed;
      m_highQueueStats.detailedQlength = detailed;

      // If not detailed, set up interval-based recording
      if (!detailed)
      {
          StringValue sv;
          if (GlobalValue::GetValueByNameFailSafe("qlengthRecordInterval", sv))
          {
              m_lowQueueStats.qlengthRecordInterval = Time(sv.Get());
              m_highQueueStats.qlengthRecordInterval = Time(sv.Get());
          }
      }
  }
  
  NdpSwitchQueue::~NdpSwitchQueue()
  {
      NS_LOG_FUNCTION(this);
  }
  
 void
 NdpSwitchQueue::DoDispose()
 {
     NS_LOG_FUNCTION(this);
     m_lowQueue.clear();
     m_highQueue.clear();
     m_trimDecision = nullptr;
     QueueDisc::DoDispose();
 }
  
  bool
  NdpSwitchQueue::IsControlPacket(Ptr<QueueDiscItem> item) const
  {
      NdpHeader ndpHeader;
      if (item->GetPacket()->PeekHeader(ndpHeader))
      {
          return ndpHeader.IsAck() || ndpHeader.IsNack() || ndpHeader.IsPull() ||
                 ndpHeader.IsTrim();
      }
      
      return false;
  }
  
 Ptr<QueueDiscItem>
 NdpSwitchQueue::TrimPacket(Ptr<QueueDiscItem> item)
 {
     NS_LOG_FUNCTION(this << item);
     
     // Cast to Ipv4QueueDiscItem to access IP header
     Ptr<Ipv4QueueDiscItem> ipv4Item = DynamicCast<Ipv4QueueDiscItem>(item);
     if (!ipv4Item)
     {
         NS_LOG_WARN("Cannot trim non-IPv4 packet");
         return nullptr;
     }
     
     Ptr<Packet> packet = item->GetPacket()->Copy();
     
     // Extract NDP header
     NdpHeader ndpHeader;
     packet->RemoveHeader(ndpHeader);
     
     // Mark as trimmed
     ndpHeader.AddFlag(NdpHeader::TRIM);
     
     // Create new packet with only header (no payload)
     Ptr<Packet> trimmedPacket = Create<Packet>(0);
     trimmedPacket->AddHeader(ndpHeader);
     
     // Create new IPv4 queue disc item with trimmed packet
     Ipv4Header ipHeader = ipv4Item->GetHeader();
     Ptr<Ipv4QueueDiscItem> trimmedItem = Create<Ipv4QueueDiscItem>(trimmedPacket,
                                                                     item->GetAddress(),
                                                                     item->GetProtocol(),
                                                                     ipHeader);
     
     m_trimCount++;
     m_traceTrim(trimmedItem);
     
     NS_LOG_DEBUG("Trimmed packet: connId=" << ndpHeader.GetConnectionId()
                                            << " seq=" << ndpHeader.GetSequence());
     
     return trimmedItem;
 }
  
Ptr<QueueDiscItem>
NdpSwitchQueue::ReturnToSender(Ptr<QueueDiscItem> item)
{
    NS_LOG_FUNCTION(this << item);
    
    // Cast to Ipv4QueueDiscItem
    Ptr<Ipv4QueueDiscItem> ipv4Item = DynamicCast<Ipv4QueueDiscItem>(item);
    if (!ipv4Item)
    {
        NS_LOG_WARN("Cannot perform RTS on non-IPv4 packet");
        return nullptr;
    }
    
    // Get IPv4 header and swap source/destination
    Ipv4Header ipHeader = ipv4Item->GetHeader();
    Ipv4Address srcAddr = ipHeader.GetSource();
    Ipv4Address dstAddr = ipHeader.GetDestination();
    
    // Extract NDP header to get sequence number and connection ID
    Ptr<Packet> tempPacket = item->GetPacket()->Copy();
    NdpHeader ndpHeader;
    if (tempPacket->GetSize() >= ndpHeader.GetSerializedSize())
    {
        tempPacket->RemoveHeader(ndpHeader);
    }
    
    // ✅ Build RTS packet:
    //   - Strip payload (header-only, TRIM flag)
    //   - Swap IP src/dst so packet routes back to original sender
    //   - Add RTS flag so sender's ForwardUp() recognises it
    //
    // ✅ Inject via SwitchNode::SendIpv4Packet so the switch's ECMP routing
    //   picks the correct EGRESS interface toward the original sender.
    //   (Placing the packet in m_highQueue would keep it on the current egress
    //    interface toward the RECEIVER, which is wrong.)

    // Build trimmed NDP header: TRIM + RTS flags, original seq/connId preserved
    ndpHeader.AddFlag(NdpHeader::TRIM);
    ndpHeader.AddFlag(NdpHeader::RTS);

    Ptr<Packet> rtsPayload = Create<Packet>(0);   // no payload
    rtsPayload->AddHeader(ndpHeader);

    // Build new IPv4 header with swapped src/dst
    Ipv4Header rtsIpHeader = ipHeader;
    rtsIpHeader.SetSource(dstAddr);       // original destination → new source
    rtsIpHeader.SetDestination(srcAddr);  // original source → new destination
    rtsIpHeader.SetTtl(64);
    rtsIpHeader.EnableChecksum();

    // Prepend IP header (SendIpv4Packet strips it internally)
    rtsPayload->AddHeader(rtsIpHeader);

    // 🔍 DEBUG: RTS creation
    // std::cout << "🔄 [RTS-CREATE] Node=" << nodeId
    //           << " Time=" << now.GetMicroSeconds() << "us"
    //           << " Seq=" << seq
    //           << " ConnId=" << connId
    //           << " Src=" << srcAddr << " → Dst=" << dstAddr
    //           << " LowQ=" << m_lowQueue.size() << "/" << m_lowQueueMaxPackets
    //           << " HighQ=" << m_highQueue.size() << "/" << m_highQueueMaxPackets
    //           << " RTS#=" << (m_returnToSenderCount + 1)
    //           << std::endl;

    // Inject into switch ECMP routing → correct egress toward original sender
    // QueueDisc doesn't expose GetNetDevice(); use Simulator::GetContext() → NodeList
    uint32_t ctxId = Simulator::GetContext();
    Ptr<Node> ctxNode = (ctxId != Simulator::NO_CONTEXT) ? NodeList::GetNode(ctxId) : nullptr;
    Ptr<SwitchNode> sw = DynamicCast<SwitchNode>(ctxNode);
    Ptr<NetDevice> netDevice = (ctxNode && ctxNode->GetNDevices() > 0)
                                    ? ctxNode->GetDevice(0) : nullptr;
    if (sw)
    {
        // SendIpv4Packet(inDev, pkt):
        //   - inDev used only for PER_PACKET_SYMMETRIC mode; can be any dev otherwise
        //   - Reads IP dst from pkt to select egress via m_routeTable (ECMP)
        sw->SendIpv4Packet(netDevice, rtsPayload);

        m_returnToSenderCount++;
        m_traceReturnToSender(ipv4Item);  // trace original item for stats

        NS_LOG_INFO("RTS injected via SwitchNode routing: " << srcAddr << " → " << dstAddr
                    << " (count=" << m_returnToSenderCount << ")");
        return nullptr;   // ← do NOT queue in m_highQueue
    }

    // Fallback (non-SwitchNode topology): fall back to highQueue approach
    NS_LOG_WARN("ReturnToSender: node is not SwitchNode, falling back to highQueue");

    rtsPayload->RemoveHeader(rtsIpHeader);   // strip IP header we just added

    Ptr<Ipv4QueueDiscItem> rtsItem = Create<Ipv4QueueDiscItem>(rtsPayload,
                                                                item->GetAddress(),
                                                                item->GetProtocol(),
                                                                rtsIpHeader);
    m_returnToSenderCount++;
    m_traceReturnToSender(rtsItem);
    return rtsItem;
}
  
bool
NdpSwitchQueue::DoEnqueue(Ptr<QueueDiscItem> item)
{
    NS_LOG_FUNCTION(this << item);
    
    // Check if this is a control packet
    bool isControl = IsControlPacket(item);
    
    // ── Global event counters ─────────────────────────────────────────────────
    if (isControl) { g_sw_ctrl_in++; } else { g_sw_data_in++; }

    if (isControl)
    {
        // Control packet goes to high queue
        if (m_highQueue.size() < m_highQueueMaxPackets)
        {
            m_highQueue.push_back(item);
            m_highQueueStats.currentBytes += item->GetSize();
            PacketEnqueued(item);
            m_ndpTraceEnqueue(item);
            RecordHighQueueLength();
            
            NS_LOG_DEBUG("Enqueued control packet to highQueue (size="
                         << m_highQueue.size() << ")");
            return true;
        }
        else
        {
            // High queue is full - DROP control packets (NOT RTS!)
            g_sw_ctrl_drop++;
            m_ndpTraceDrop(item);
            NS_LOG_DEBUG("Dropped control packet (highQueue full)");
            DropBeforeEnqueue(item, "High queue full");
            return false;
        }
    }
    else
    {
        // Data packet goes to low queue
        if (m_lowQueue.size() < m_lowQueueMaxPackets)
        {
            m_lowQueue.push_back(item);
            m_lowQueueStats.currentBytes += item->GetSize();
            PacketEnqueued(item);
            m_ndpTraceEnqueue(item);
            RecordLowQueueLength();
            
            NS_LOG_DEBUG("Enqueued data packet to lowQueue (size=" << m_lowQueue.size() << ")");
            return true;
        }
        else
        {
            // Low queue is full - apply trim-instead-of-drop
            // With 50% probability: trim arriving packet (head trim)
            // With 50% probability: trim tail packet and enqueue arriving packet (tail trim)
            
            NS_LOG_INFO("Low queue full (size=" << m_lowQueue.size() 
                       << "), applying trim mechanism");
            
            double decision = m_trimDecision->GetValue();
            
            if (decision < 0.5)
            {
                // Trim arriving packet (head trim)
                Ptr<QueueDiscItem> trimmedItem = TrimPacket(item);
                m_headTrimCount++;
                g_sw_head_trim++;
                
                NS_LOG_DEBUG("Head trim: trimmed arriving packet (totalTrims=" 
                            << (m_headTrimCount + m_tailTrimCount) << ")");
                
                // Enqueue trimmed header to high queue
                if (m_highQueue.size() < m_highQueueMaxPackets)
                {
                    m_highQueue.push_back(trimmedItem);
                    m_highQueueStats.currentBytes += trimmedItem->GetSize();
                    PacketEnqueued(trimmedItem);
                    m_ndpTraceEnqueue(trimmedItem);
                    
                    NS_LOG_DEBUG("Head trim: trimmed header enqueued to HighQueue (size="
                                 << m_highQueue.size() << ")");
                    
                    return true;
                }
                else
                {
                    // HighQueue also full → LowQ满 + HighQ满 = RTS条件满足
                    // 对原始 data packet 执行 Return-to-Sender
                    NS_LOG_INFO("RTS condition met (head-trim): LowQueue="
                                << m_lowQueue.size() << "/" << m_lowQueueMaxPackets
                                << " HighQueue=" << m_highQueue.size()
                                << "/" << m_highQueueMaxPackets);

                    // Drop the trimmed header (HighQ full, cannot enqueue)
                    m_ndpTraceDrop(trimmedItem);

                    // Perform RTS on the ORIGINAL data packet.
                    //
                    // ReturnToSender() has two outcomes:
                    //   nullptr  → RTS was already injected via SwitchNode routing (normal path)
                    //   non-null → fallback path (non-SwitchNode); HighQ is still full so drop it
                    g_sw_rts++;
                    Ptr<QueueDiscItem> rtsItem = ReturnToSender(item);
                    if (rtsItem)
                    {
                        // Fallback path: HighQ is still full → discard the RTS item.
                        // Sender will recover via RTO.
                        m_ndpTraceDrop(rtsItem);
                        NS_LOG_WARN("Head-trim RTS: HighQ still full in fallback path, RTS dropped");
                    }
                    // (nullptr → RTS already sent via SwitchNode routing, nothing more to do)

                    DropBeforeEnqueue(item, "RTS – both queues full (head-trim)");
                    return false;
                }
            }
            else
            {
                // Trim tail packet (tail trim)
                if (m_lowQueue.empty())
                {
                    // Safety: queue empty, fall back to head trim
                    Ptr<QueueDiscItem> trimmedItem = TrimPacket(item);
                    m_headTrimCount++;
                    
                    if (m_highQueue.size() < m_highQueueMaxPackets)
                    {
                        m_highQueue.push_back(trimmedItem);
                        m_highQueueStats.currentBytes += trimmedItem->GetSize();
                        PacketEnqueued(trimmedItem);
                        m_ndpTraceEnqueue(trimmedItem);
                        return true;
                    }
                    m_ndpTraceDrop(trimmedItem);
                    DropBeforeEnqueue(item, "High queue full");
                    return false;
                }
                
                // ✅ SPEC-CHECK: Before popping tail, verify HighQueue has space for trimmed header.
                // If HighQueue is also full → both queues are full → RTS the arriving packet,
                // do NOT admit it, do NOT eject the tail (per spec: 不进入任何队列).
                if (m_highQueue.size() >= m_highQueueMaxPackets)
                {
                    // Low满 + High满 → RTS arriving packet
                    NS_LOG_INFO("Tail-trim aborted: RTS condition met (LowQ="
                                << m_lowQueue.size() << "/" << m_lowQueueMaxPackets
                                << " HighQ=" << m_highQueue.size() << "/" << m_highQueueMaxPackets
                                << ")");
                    
                    g_sw_rts++;
                    Ptr<QueueDiscItem> rtsItem = ReturnToSender(item);  // arriving packet → RTS
                    if (rtsItem)
                    {
                        // High is full, so RTS is also dropped (extreme case)
                        m_ndpTraceDrop(rtsItem);
                        NS_LOG_WARN("Tail-trim RTS: HighQ full, RTS dropped");
                    }
                    DropBeforeEnqueue(item, "RTS – both queues full");
                    return false;
                }
                
                // HighQueue has space: proceed with normal tail-trim
                // Remove tail packet from low queue
                Ptr<QueueDiscItem> tailItem = m_lowQueue.back();
                m_lowQueueStats.currentBytes -= tailItem->GetSize();
                m_lowQueue.pop_back();
                
                // Trim the tail packet
                Ptr<QueueDiscItem> trimmedItem = TrimPacket(tailItem);
                m_tailTrimCount++;
                g_sw_tail_trim++;
                
                NS_LOG_DEBUG("Tail trim: trimmed tail packet (totalTrims=" 
                            << (m_headTrimCount + m_tailTrimCount) << ")");
                
                // Enqueue trimmed tail header to high queue (space confirmed above)
                m_highQueue.push_back(trimmedItem);
                m_highQueueStats.currentBytes += trimmedItem->GetSize();
                PacketEnqueued(trimmedItem);
                m_ndpTraceEnqueue(trimmedItem);
                
                // Enqueue the arriving packet in the freed low-queue slot
                m_lowQueue.push_back(item);
                m_lowQueueStats.currentBytes += item->GetSize();
                PacketEnqueued(item);
                m_ndpTraceEnqueue(item);
                RecordLowQueueLength();
                return true;
            }
        }
    }
    
    // This should never be reached
    NS_LOG_ERROR("DoEnqueue reached unreachable code - this is a bug!");
    return false;
 }
  
Ptr<QueueDiscItem>
NdpSwitchQueue::DoDequeue()
{
    NS_LOG_FUNCTION(this);

    // ── Strict Priority scheduling (NDP paper §3.2) ────────────────────────
    //
    //  Control packets (trim headers, ACKs, NACKs, PULLs) in the HIGH queue
    //  always have strict priority over data packets in the LOW queue.
    //
    //  Rationale (Handley et al., SIGCOMM 2017):
    //    "Headers have strict priority over data, ensuring that congestion
    //     signals are never delayed."
    //
    //  This works because control packets are header-only (~60-80 B) and
    //  consume negligible bandwidth even when numerous:
    //    e.g. 10,000 trim headers × 80 B × 8 = 6.4 Mbit ≈ 0.064 ms @100 Gbps
    //
    //  The previous WRR 10:1 (by packet count) inadvertently throttled data
    //  to 1/11 of dequeue slots.  Since data packets are ~19× larger than
    //  control packets, this meant data only got ~65% of link bandwidth,
    //  causing a positive-feedback trim loop (88% trim rate in steady state).
    //
    //  With strict priority the steady-state trim rate drops to near 0%
    //  because control headers drain almost instantly, leaving the full
    //  link bandwidth available for data.
    // ────────────────────────────────────────────────────────────────────────

    if (m_highQueue.empty() && m_lowQueue.empty())
    {
        NS_LOG_DEBUG("Both queues empty");
        return nullptr;
    }

    // ── HIGH queue (control) always first ────────────────────────────────
    if (!m_highQueue.empty())
    {
        Ptr<QueueDiscItem> item = m_highQueue.front();
        m_highQueueStats.currentBytes -= item->GetSize();
        m_highQueue.pop_front();
        g_sw_ctrl_dequeued++;
        RecordHighQueueLength();

        m_ndpTraceDequeue(item);
        NS_LOG_DEBUG("SP dequeue HIGH (highQ=" << m_highQueue.size()
                     << " lowQ=" << m_lowQueue.size() << ")");
        return item;
    }

    // ── LOW queue (data) only when HIGH is empty ─────────────────────────
    if (!m_lowQueue.empty())
    {
        Ptr<QueueDiscItem> item = m_lowQueue.front();
        m_lowQueueStats.currentBytes -= item->GetSize();
        m_lowQueue.pop_front();
        RecordLowQueueLength();

        m_ndpTraceDequeue(item);
        NS_LOG_DEBUG("SP dequeue LOW (highQ=" << m_highQueue.size()
                     << " lowQ=" << m_lowQueue.size() << ")");
        return item;
    }

    return nullptr;
}

Ptr<const QueueDiscItem>
NdpSwitchQueue::DoPeek()
{
    NS_LOG_FUNCTION(this);

    // Mirror the strict-priority order of DoDequeue (without removing)
    if (!m_highQueue.empty())
    {
        return m_highQueue.front();
    }
    if (!m_lowQueue.empty())
    {
        return m_lowQueue.front();
    }
    return nullptr;
}
  
 bool
 NdpSwitchQueue::CheckConfig()
 {
     NS_LOG_FUNCTION(this);
     return true;
 }
 
void
NdpSwitchQueue::InitializeParams()
{
    NS_LOG_FUNCTION(this);
}

void
NdpSwitchQueue::Run()
{
    NS_LOG_FUNCTION(this);
    
    // Similar to PausableQueueDisc::Run(), we dequeue only ONE packet
    // and do NOT call RunEnd() unless the queue is empty.
    // This allows packets to accumulate in the queue.
    
    if (RunBegin())  // Check if already running (m_running flag)
    {
        // Dequeue only ONE packet (not a loop like standard QueueDisc::Run())
        Ptr<QueueDiscItem> item = DoDequeue();
        
        if (item)
        {
            // Add header back to the packet
            item->AddHeader();
            
            // Send to NetDevice
            NS_ASSERT_MSG(m_send, "Send callback not set");
            m_send(item);
            
            // 🔥 KEY POINT: Do NOT call RunEnd() here!
            // The m_running flag stays true, preventing subsequent Run() calls
            // from executing until RunEnd() is called by NetDevice::TransmitComplete()
            
            NS_LOG_DEBUG("NdpSwitchQueue::Run() dequeued 1 packet, m_running stays true");
        }
        else
        {
            // Queue is empty, release the running flag
            RunEnd();
            NS_LOG_DEBUG("NdpSwitchQueue::Run() queue empty, called RunEnd()");
        }
    }
    else
    {
        NS_LOG_DEBUG("NdpSwitchQueue::Run() already running, skipping");
    }
    
    // NOTE: RunEnd() will be called by DcbNetDevice::TransmitComplete()
    // or similar NetDevice implementation after packet transmission completes.
    // This is the key difference from standard QueueDisc::Run() which calls
    // RunEnd() immediately, preventing queue buildup.
}
  
  uint32_t
  NdpSwitchQueue::GetLowQueueSize() const
  {
      return m_lowQueue.size();
  }
  
  uint32_t
  NdpSwitchQueue::GetHighQueueSize() const
  {
      return m_highQueue.size();
  }
  
  uint64_t
  NdpSwitchQueue::GetTrimCount() const
  {
      return m_trimCount;
  }
  
  uint64_t
  NdpSwitchQueue::GetReturnToSenderCount() const
  {
      return m_returnToSenderCount;
  }

  // ── Queue length recording (incremental byte tracking, O(1) per call) ───
  void
  NdpSwitchQueue::RecordLowQueueLength()
  {
      uint32_t lowQPkts = m_lowQueue.size();
      uint32_t lowQBytes = m_lowQueueStats.currentBytes;

      if (lowQPkts > m_lowQueueStats.maxQLengthPackets)
          m_lowQueueStats.maxQLengthPackets = lowQPkts;
      if (lowQBytes > m_lowQueueStats.maxQLengthBytes)
          m_lowQueueStats.maxQLengthBytes = lowQBytes;

      if (m_lowQueueStats.detailedQlength)
      {
          m_lowQueueStats.vQLengthBytes.emplace_back(Simulator::Now(), lowQBytes);
      }
      else if (m_lowQueueStats.qlengthRecordInterval.IsStrictlyPositive() &&
               m_lowQueueStats.qlengthRecordEvent.IsExpired())
      {
          RecordQLengthIntervalic(m_lowQueueStats);
      }
  }

  void
  NdpSwitchQueue::RecordHighQueueLength()
  {
      uint32_t highQPkts = m_highQueue.size();
      uint32_t highQBytes = m_highQueueStats.currentBytes;

      if (highQPkts > m_highQueueStats.maxQLengthPackets)
          m_highQueueStats.maxQLengthPackets = highQPkts;
      if (highQBytes > m_highQueueStats.maxQLengthBytes)
          m_highQueueStats.maxQLengthBytes = highQBytes;

      if (m_highQueueStats.detailedQlength)
      {
          m_highQueueStats.vQLengthBytes.emplace_back(Simulator::Now(), highQBytes);
      }
      else if (m_highQueueStats.qlengthRecordInterval.IsStrictlyPositive() &&
               m_highQueueStats.qlengthRecordEvent.IsExpired())
      {
          RecordQLengthIntervalic(m_highQueueStats);
      }
  }

  // ── Interval-based recording (same as FifoQueueDiscEcn) ───────────────────
  void
  NdpSwitchQueue::RecordQLengthIntervalic(QueueStats& qs)
  {
      if (!qs.detailedQlength && qs.currentBytes != 0)
      {
          qs.vQLengthBytes.emplace_back(Simulator::Now(), qs.currentBytes);
          qs.qlengthRecordEvent =
              Simulator::Schedule(qs.qlengthRecordInterval,
                                  &NdpSwitchQueue::RecordQLengthIntervalic,
                                  this,
                                  std::ref(qs));
      }
  }
  
  } // namespace ns3
  