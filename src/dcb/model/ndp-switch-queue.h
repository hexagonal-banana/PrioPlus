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
 * Implements:
 * - Dual queues (lowQueue for data, highQueue for control)
 * - Trim-instead-of-drop with 50% tail-trim probability
 * - Weighted round-robin scheduling (10:1 highQueue:lowQueue)
 * - Return-to-sender for header overflow
 */

 #ifndef NDP_SWITCH_QUEUE_H
 #define NDP_SWITCH_QUEUE_H
 
#include "ns3/queue-disc.h"
#include "ns3/traced-callback.h"
#include "ns3/random-variable-stream.h"
#include "ns3/nstime.h"

#include <deque>
#include <vector>
#include <utility>

namespace ns3
{

/**
 * \ingroup dcb
 * \brief NDP Switch Queue with dual priority queues
 *
 * This queue disc implements the NDP switch service model:
 * - lowQueue: data packets (limited capacity, ~8 packets)
 * - highQueue: headers, ACK, NACK, PULL (higher priority)
 * 
 * On data packet arrival when lowQueue is full:
 * - With 50% probability: trim arriving packet
 * - With 50% probability: trim tail packet from lowQueue
 * 
 * Scheduling: weighted round-robin (10:1 highQueue:lowQueue)
 * 
 * Return-to-sender: when highQueue is full, reverse header and send back
 */
class NdpSwitchQueue : public QueueDisc
 {
   public:
     /**
      * \brief Get the type ID.
      * \return the object TypeId
      */
     static TypeId GetTypeId();
 
    NdpSwitchQueue();
    virtual ~NdpSwitchQueue();
 
     /**
      * \brief Get low queue occupancy
      * \return number of packets in low queue
      */
     uint32_t GetLowQueueSize() const;
 
     /**
      * \brief Get high queue occupancy
      * \return number of packets in high queue
      */
     uint32_t GetHighQueueSize() const;
 
     /**
      * \brief Get number of trims
      * \return trim count
      */
     uint64_t GetTrimCount() const;
 
     /**
      * \brief Get number of return-to-sender events
      * \return RTS count
      */
     uint64_t GetReturnToSenderCount() const;

     /**
      * \brief Override Run() to allow queue buildup
      * 
      * This method dequeues only ONE packet (unlike standard QueueDisc::Run()
      * which loops until the queue is empty). This allows packets to accumulate
      * in the queue, enabling the Trim mechanism to trigger.
      * 
      * The method does NOT call RunEnd() after dequeuing, keeping m_running = true.
      * RunEnd() will be called by NetDevice::TransmitComplete() after the packet
      * transmission completes.
      */
     void Run() override;

  protected:
    void DoDispose() override;

  private:
    bool DoEnqueue(Ptr<QueueDiscItem> item) override;
    Ptr<QueueDiscItem> DoDequeue() override;
    Ptr<const QueueDiscItem> DoPeek() override;
    bool CheckConfig() override;
    void InitializeParams() override;
     /**
      * \brief Check if a packet is a control packet (ACK, NACK, PULL, or trimmed header)
      * \param item the queue disc item
      * \return true if control packet
      */
     bool IsControlPacket(Ptr<QueueDiscItem> item) const;
 
     /**
      * \brief Trim a packet (remove payload, keep header only)
      * \param item the queue disc item to trim
      * \return the trimmed item
      */
     Ptr<QueueDiscItem> TrimPacket(Ptr<QueueDiscItem> item);
 
     /**
      * \brief Perform return-to-sender operation
      * \param item the trimmed header to send back
      * \return the return-to-sender item
      */
     Ptr<QueueDiscItem> ReturnToSender(Ptr<QueueDiscItem> item);
 
     // Queue parameters
     uint32_t m_lowQueueMaxPackets;    ///< Max packets in low queue (~8)
     uint32_t m_highQueueMaxPackets;   ///< Max packets in high queue
     uint32_t m_highQueueWeight;       ///< High queue weight for WRR (10)
     uint32_t m_lowQueueWeight;        ///< Low queue weight for WRR (1)
 
     // Queues
     std::deque<Ptr<QueueDiscItem>> m_lowQueue;   ///< Data packet queue
     std::deque<Ptr<QueueDiscItem>> m_highQueue;  ///< Control packet queue
 
     // Scheduling state
     uint32_t m_highQueueCredits;  ///< Remaining high-queue credits in current WRR round
     uint32_t m_lowQueueCredits;   ///< Remaining low-queue credits in current WRR round
 
    // Statistics
     uint64_t m_trimCount;          ///< Number of packets trimmed
     uint64_t m_returnToSenderCount; ///< Number of return-to-sender events
     uint64_t m_tailTrimCount;      ///< Number of tail trims
     uint64_t m_headTrimCount;      ///< Number of head (arriving) trims

     // Random variable for trim decision
     Ptr<UniformRandomVariable> m_trimDecision;

     // Traced callbacks (NDP-specific)
     TracedCallback<Ptr<const QueueDiscItem>> m_ndpTraceEnqueue;
     TracedCallback<Ptr<const QueueDiscItem>> m_ndpTraceDequeue;
     TracedCallback<Ptr<const QueueDiscItem>> m_ndpTraceDrop;
     TracedCallback<Ptr<const QueueDiscItem>> m_traceTrim;
     TracedCallback<Ptr<const QueueDiscItem>> m_traceReturnToSender;

  public:
    // ── Queue length statistics (for JSON output, mirrors FifoQueueDiscEcn) ──
    struct QueueStats
    {
        uint32_t maxQLengthPackets{0};
        uint32_t maxQLengthBytes{0};
        uint32_t currentBytes{0};        ///< Running byte count (updated on enq/deq)
        bool detailedQlength{false};     ///< Record every enqueue/dequeue
        /// Interval-based recording (used when detailedQlength == false)
        Time qlengthRecordInterval;
        EventId qlengthRecordEvent;
        /// Time-series: {time, queueBytes}
        std::vector<std::pair<Time, uint32_t>> vQLengthBytes;
    };

    /** Get low-queue (data) statistics. */
    const QueueStats& GetLowQueueStats() const { return m_lowQueueStats; }
    /** Get high-queue (control) statistics. */
    const QueueStats& GetHighQueueStats() const { return m_highQueueStats; }

  private:
    QueueStats m_lowQueueStats;       ///< Per-port low-queue (data) stats
    QueueStats m_highQueueStats;      ///< Per-port high-queue (control) stats
    void RecordLowQueueLength();      ///< Record low-queue length
    void RecordHighQueueLength();     ///< Record high-queue length
    void RecordQLengthIntervalic(QueueStats& qs); ///< Interval-based recording
};
 
 } // namespace ns3
 
 #endif /* NDP_SWITCH_QUEUE_H */
 