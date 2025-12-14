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

#ifndef PAUSABLE_QUEUE_DISC_H
#define PAUSABLE_QUEUE_DISC_H

#include "fifo-queue-disc-ecn.h"

#include "ns3/node.h"
#include "ns3/queue-disc.h"
#include "ns3/queue-item.h"
#include "ns3/type-id.h"
#include "leaky-bucket.h"

class LeakyBucket;
namespace ns3
{

struct EcnConfig;

class PausableQueueDiscClass : public QueueDiscClass
{
  public:
    static TypeId GetTypeId();

    PausableQueueDiscClass();
    virtual ~PausableQueueDiscClass();

    bool IsPaused() const;

    void SetPaused(bool paused);

    void SetWdrrParameters(uint32_t quantum, uint32_t maxCredit);

    void IncrementCredit();

    void DecrementCredit(uint32_t size);

    bool HasCredit() const;

  private:
    bool m_isPaused;

    /***** Members for WDRR *****/
    int32_t m_quantum;  // The quantum for WDRR, in bytes
    int32_t m_credit;    // The credit for WDRR, in bytes, can be negative
    int32_t m_maxCredit; // The maximum credit for WDRR, in bytes
}; // PausableQueueDiscClass

class PausableQueueDisc : public QueueDisc
{
  public:
    static TypeId GetTypeId(void);

    PausableQueueDisc();
    PausableQueueDisc(uint32_t port);
    PausableQueueDisc(Ptr<Node> node, uint32_t port);

    virtual ~PausableQueueDisc();

    /**
     * \brief Get the i-th queue disc class
     * \param i the index of the queue disc class
     * \return the i-th queue disc class.
     */
    Ptr<PausableQueueDiscClass> GetQueueDiscClass(std::size_t i) const;

    virtual void Run(void) override;

    void SetNode(Ptr<Node> node);
    void SetPortIndex(uint32_t portIndex);
    void SetFCEnabled(bool enable);
    void SetQueueSize(QueueSize qSize);

    void SetPaused(uint32_t priority, bool paused);

    typedef Callback<void, uint32_t, uint32_t, Ptr<Packet>> TCEgressCallback;

    /**
     * \brief Called when install the qdisc, to register the callback called after packet is
     * dequeued
     */
    void RegisterTrafficControlCallback(TCEgressCallback cb);

    /**
     * \brief Get the i-th inner queue size.
     * Used by host's socket to determine whether to send more packets down.
     */
    QueueSize GetInnerQueueSize(uint32_t priority) const;

    std::pair<uint32_t, uint32_t> GetNodeAndPortId() const;

    /**
     * \brief Called by RoCEv2L4Protocol to register a callback function to notify the RoCEv2L4Proto
     * to send a packet down.
     */
    void RegisterSendDataCallback(Callback<void, uint32_t, uint32_t> cb);

    /**
     * \brief Called to set ECN threshold for all inner queues manually.
     * \param kmin the minimum threshold
     * \param kmax the maximum threshold
     */
    void SetEcnThres(std::string kmin, std::string kmax);

    class Stats
    {
      public:
        // constructor
        Stats(Ptr<PausableQueueDisc> qdisc);

        Ptr<PausableQueueDisc> m_qdisc;

        bool bDetailedQlengthStats;
        std::vector<std::tuple<Time, uint8_t, bool>>
            vPauseResumeTime; //<! Record the pause/resume time and the priority, true for paused,
                              // false for resumed

        // For now we only support FifoQueueDiscEcn as inner queue
        std::vector<std::shared_ptr<FifoQueueDiscEcn::Stats>>
            vQueueStats; //<! The queue statistics for each inner queue
        // Recorder function
        void RecordPauseResume(uint32_t prio, bool paused);

        // Collect the statistics and check if the statistics is correct
        void CollectAndCheck();

        // No getter for simplicity
    };

    std::shared_ptr<Stats> GetStats() const;

    /**
     * \brief Set the detailedSwitchStats for the queue disc and inner queue.
     *
     * This function is often used to disable the statistics collection for the queue disc and inner
     * queue. It will break the design of stats configuration, but the switch's stats is so heavy
     * that we have to do this. Sorry for that.
     */
    void SetDetailedSwitchStats(bool bDetailedQlengthStats);

    /**
     * \brief Set the WDRR parameters for the queue disc.
     */
    void SetWdrrParameters(std::vector<uint32_t> priorities,
                           std::vector<uint32_t> quantum,
                           uint32_t maxCredit);
    /**
     * \brief Default configuration, construct a strict priority m_priorityToInnerQueue.
     */
    void SetDefaultStrictPriority();
    void SetPriorityRateLimits(const std::vector<std::tuple<uint32_t, std::string, uint32_t>>& rateLimits);
    bool CheckRateLimit(uint32_t priority, uint32_t size);
  protected:
    Ptr<Node> m_node; //!< Node owning this NetDevice

    bool m_fcEnabled;    // Whether flow control is enabled, once enabled, the queue disc will be
                         // pausable
    int32_t m_portIndex; //!< the port index this QueueDisc belongs to
    TCEgressCallback m_tcEgress; // Costum callback, called after packet is dequeued

    QueueSize m_queueSize;
    /// Traced callback: fired when a packet is enqueued, trace with item, host and port id,
    /// priority
    TracedCallback<Ptr<const QueueDiscItem>, std::pair<uint32_t, uint32_t>, uint32_t>
        m_traceEnqueueWithId;

    Callback<void, uint32_t, uint32_t>
        m_sendDataCallback; //<! Callback function to notify the RoCEv2L4Proto
                            // to send a packet down
  private:
    virtual bool DoEnqueue(Ptr<QueueDiscItem> item) override;

    virtual Ptr<QueueDiscItem> DoDequeue(void) override;

    virtual Ptr<const QueueDiscItem> DoPeek(void) override;

    virtual bool CheckConfig(void) override;

    virtual void InitializeParams(void) override;

    std::shared_ptr<Stats> m_stats;

    /***** Members for WDRR *****/
    std::map<uint32_t, std::vector<uint32_t>>
        m_priorityToInnerQueue; //<! Map from priority to inner queue index
    std::map<uint32_t, Ptr<LeakyBucket>>
        m_priorityToLeakyBucket; //<! Map from priority to leaky bucket for rate limiting
    
}; // class PausableQueueDisc

} // namespace ns3

#endif // PAUSABLE_QUEUE_DISC_H