#ifndef NDP_HOST_QUEUE_H
#define NDP_HOST_QUEUE_H

#include "ns3/queue-disc.h"
#include <deque>

namespace ns3
{

/**
 * \brief Simple FIFO queue for NDP host NetDevice output
 * 
 * This is a lightweight FIFO queue used on NDP host interfaces.
 * Unlike NdpSwitchQueue, it does not distinguish between data and control packets,
 * and does not implement trim mechanism.
 * 
 * The actual flow control logic (send/receive queues, PULL queue) is handled
 * by NdpSocket at the transport layer.
 */
class NdpHostQueue : public QueueDisc
{
  public:
    /**
     * \brief Get the type ID
     * \return the object TypeId
     */
    static TypeId GetTypeId();

    /**
     * \brief Constructor
     */
    NdpHostQueue();

    /**
     * \brief Destructor
     */
    ~NdpHostQueue() override;

  private:
    // Inherited from QueueDisc
    bool DoEnqueue(Ptr<QueueDiscItem> item) override;
    Ptr<QueueDiscItem> DoDequeue() override;
    Ptr<const QueueDiscItem> DoPeek() override;
    bool CheckConfig() override;
    void InitializeParams() override;

    /// Maximum number of packets in queue
    uint32_t m_maxPackets;
    
    /// Simple FIFO queue
    std::deque<Ptr<QueueDiscItem>> m_queue;
};

} // namespace ns3

#endif /* NDP_HOST_QUEUE_H */
