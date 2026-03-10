#include "ndp-host-queue.h"
#include "ns3/log.h"
#include "ns3/uinteger.h"
#include "ns3/simulator.h"
#include <iomanip>
#include <iostream>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("NdpHostQueue");

// Global counters for all NdpHostQueue instances
static uint64_t g_hq_enqueued = 0;
static uint64_t g_hq_dropped  = 0;
static uint64_t g_hq_dequeued = 0;
static uint32_t g_hq_peakSize = 0;

void PrintNdpHostQueueStats()
{
    std::cout << "╔══════════════════════════════════════════════════╗\n"
              << "║          NDP Host Queue Statistics               ║\n"
              << "╠══════════════════════════════════════════════════╣\n"
              << "║  Enqueued                   : " << std::setw(8) << g_hq_enqueued << "              ║\n"
              << "║  Dropped (queue full)       : " << std::setw(8) << g_hq_dropped  << "              ║\n"
              << "║  Dequeued                   : " << std::setw(8) << g_hq_dequeued << "              ║\n"
              << "║  Peak queue size            : " << std::setw(8) << g_hq_peakSize << "              ║\n"
              << "╚══════════════════════════════════════════════════╝\n";
}
NS_OBJECT_ENSURE_REGISTERED(NdpHostQueue);

TypeId
NdpHostQueue::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::NdpHostQueue")
            .SetParent<QueueDisc>()
            .SetGroupName("Dcb")
            .AddConstructor<NdpHostQueue>()
            .AddAttribute("MaxPackets",
                          "The maximum number of packets accepted by this queue",
                          UintegerValue(1000),
                          MakeUintegerAccessor(&NdpHostQueue::m_maxPackets),
                          MakeUintegerChecker<uint32_t>());
    return tid;
}

NdpHostQueue::NdpHostQueue()
    : QueueDisc(QueueDiscSizePolicy::NO_LIMITS),  // ✅ BUG-5 FIX: this queue manages its own
      m_maxPackets(1000)                           // std::deque (m_queue) and does NOT use any
                                                   // ns-3 internal queue, so NO_LIMITS is correct.
                                                   // SINGLE_INTERNAL_QUEUE was contradicted by
                                                   // CheckConfig() rejecting internal queues and
                                                   // caused GetCurrentSize() to always return 0.
{
    NS_LOG_FUNCTION(this);
}

NdpHostQueue::~NdpHostQueue()
{
    NS_LOG_FUNCTION(this);
}

bool
NdpHostQueue::DoEnqueue(Ptr<QueueDiscItem> item)
{
    NS_LOG_FUNCTION(this << item);

    if (m_queue.size() >= m_maxPackets)
    {
        NS_LOG_DEBUG("Queue full (" << m_queue.size() << "/" << m_maxPackets 
                     << "), dropping packet");
        g_hq_dropped++;
        DropBeforeEnqueue(item, "Queue full");
        return false;
    }

    m_queue.push_back(item);
    g_hq_enqueued++;
    if (m_queue.size() > g_hq_peakSize) { g_hq_peakSize = m_queue.size(); }
    
    // uint32_t nodeId = Simulator::GetContext();
    // std::cout << "        🟢 [HostQ-Enq] Node=" << nodeId
    //           << " QSize=" << m_queue.size()
    //           << std::endl;
    
    NS_LOG_DEBUG("Enqueued packet, queue size: " << m_queue.size() 
                 << "/" << m_maxPackets);
    
    return true;
}

Ptr<QueueDiscItem>
NdpHostQueue::DoDequeue()
{
    NS_LOG_FUNCTION(this);

    if (m_queue.empty())
    {
        NS_LOG_DEBUG("Queue is empty, cannot dequeue");
        return nullptr;
    }

    Ptr<QueueDiscItem> item = m_queue.front();
    m_queue.pop_front();
    g_hq_dequeued++;
    
    // uint32_t nodeId = Simulator::GetContext();
    // std::cout << "        🟡 [HostQ-Deq] Node=" << nodeId
    //           << " QSize=" << m_queue.size()
    //           << std::endl;
    
    NS_LOG_DEBUG("Dequeued packet, queue size: " << m_queue.size() 
                 << "/" << m_maxPackets);
    
    return item;
}

Ptr<const QueueDiscItem>
NdpHostQueue::DoPeek()
{
    NS_LOG_FUNCTION(this);
    
    if (m_queue.empty())
    {
        NS_LOG_DEBUG("Queue is empty, cannot peek");
        return nullptr;
    }
    
    return m_queue.front();
}

bool
NdpHostQueue::CheckConfig()
{
    NS_LOG_FUNCTION(this);
    
    if (GetNQueueDiscClasses() > 0)
    {
        NS_LOG_ERROR("NdpHostQueue cannot have classes");
        return false;
    }

    if (GetNPacketFilters() > 0)
    {
        NS_LOG_ERROR("NdpHostQueue does not need packet filters");
        return false;
    }

    if (GetNInternalQueues() > 0)
    {
        NS_LOG_ERROR("NdpHostQueue does not need internal queues");
        return false;
    }

    return true;
}

void
NdpHostQueue::InitializeParams()
{
    NS_LOG_FUNCTION(this);
    // No special initialization needed for simple FIFO
}

} // namespace ns3
