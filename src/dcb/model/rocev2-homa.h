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
 */

#ifndef ROCEV2_HOMA_H
#define ROCEV2_HOMA_H

#include "ns3/event-id.h"
#include "ns3/ipv4-address.h"
#include "ns3/nstime.h"
#include "ns3/rocev2-header.h"
#include "ns3/rocev2-socket.h"
#include "ns3/tag.h"

#include <map>
#include <queue>

namespace ns3
{

class RoCEv2Homa;

class HomaDataTag : public Tag
{
  public:
    static TypeId GetTypeId(void);
    virtual TypeId GetInstanceTypeId(void) const;
    virtual uint32_t GetSerializedSize(void) const;
    virtual void Serialize(TagBuffer i) const;
    virtual void Deserialize(TagBuffer i);
    virtual void Print(std::ostream& os) const;

    HomaDataTag();
    HomaDataTag(uint32_t flowId, uint32_t msgSize, uint8_t isUnscheduled = 0);

    void SetFlowId(uint32_t id);
    uint32_t GetFlowId() const;
    void SetMsgSize(uint32_t size);
    uint32_t GetMsgSize() const;
    void SetIsUnscheduled(uint8_t isUnscheduled);
    uint8_t GetIsUnscheduled() const;

  private:
    uint32_t m_flowId;
    uint32_t m_msgSize;
    uint8_t m_isUnscheduled;
};

class HomaGrantTag : public Tag
{
  public:
    static TypeId GetTypeId(void);
    virtual TypeId GetInstanceTypeId(void) const;
    virtual uint32_t GetSerializedSize(void) const;
    virtual void Serialize(TagBuffer i) const;
    virtual void Deserialize(TagBuffer i);
    virtual void Print(std::ostream& os) const;

    HomaGrantTag();
    HomaGrantTag(uint32_t flowId, uint32_t grantOffset, uint32_t priority);

    void SetFlowId(uint32_t id);
    uint32_t GetFlowId() const;
    void SetGrantOffset(uint32_t os);
    uint32_t GetGrantOffset() const;
    void SetPriority(uint32_t p);
    uint32_t GetPriority() const;

  private:
    uint32_t m_flowId;
    uint32_t m_grantOffset;
    uint32_t m_priority;
};


class RoCEv2Homa;

class HomaNodeScheduler : public Object
{
  public:
    static TypeId GetTypeId(void);
    HomaNodeScheduler();
    virtual ~HomaNodeScheduler();

    void UpdateFlow(uint32_t flowId, uint32_t msgSize, uint32_t recvedBytes, Ptr<RoCEv2Homa> flow);
    void CheckSchedule(uint32_t packetSize,
                       uint32_t realSize,
                       uint32_t flowId,
                       uint32_t recvedBytes,
                       uint32_t msgSize);

    static Ptr<HomaNodeScheduler> Get(uint32_t nodeId);

  private:
    struct FlowState
    {
        uint32_t msgSize;
        uint32_t recvedBytes;
        uint32_t grantedBytes;
        uint32_t msgBytes;
        Time lastUpdate;
        Ptr<RoCEv2Homa> flow;

        EventId resendTimer;

        FlowState()
            : msgSize(0),
              recvedBytes(0),
              grantedBytes(0),
              msgBytes(0)
        {
        }
    };

    std::map<uint32_t, FlowState> m_activeFlows;
    uint32_t m_overcommitLevel;
    uint32_t m_sendPrio;
    static std::map<uint32_t, Ptr<HomaNodeScheduler>> m_nodeSchedulers;
};

class RoCEv2Homa : public RoCEv2CreditCc
{
  public:
    static TypeId GetTypeId();
    RoCEv2Homa();
    RoCEv2Homa(Ptr<RoCEv2SocketState> sockState);
    ~RoCEv2Homa() override;

    virtual std::string GetName() const;

    class Stats : public RoCEv2CongestionOps::Stats
    {
      public:
        // constructor
        Stats();

        // Detailed statistics, only enabled if needed
        bool bDetailedSenderStats;

        // Collect the statistics and check if the statistics is correct
        void CollectAndCheck();

        // No getter for simplicity
    };

    inline std::shared_ptr<RoCEv2CongestionOps::Stats> GetStats() const override
    {
        return m_stats;
    }

    virtual void Init();
    virtual void SetReady();
    void SetFlowId(uint32_t flowId);
    uint32_t GetFlowId() const;

    virtual void UpdateStateSend(Ptr<Packet> packet);
    virtual void UpdateStateRecvData(Ptr<Packet> packet, const RoCEv2Header& roce);
    virtual void UpdateStateWithRcvACK(Ptr<Packet> packet,
                                       const RoCEv2Header& roce,
                                       const uint32_t senderNextPSN);

    void SendGrantACK(uint32_t grantOffset, uint32_t priority);

    uint32_t GetNextPacketPriority(uint32_t defaultPriority) override;

    uint32_t GetUnscheduledBytes() const
    {
        return m_unscheduledBytes;
    }

    uint64_t GetGrantedBytes() const
    {
        return m_grantedBytes;
    }

    void AddGrantedBytes(uint64_t bytes)
    {
        m_grantedBytes += bytes;
    }


  private:
    // Represents one outstanding grant with its remaining credit and
    // the scheduled priority the receiver chose for that credit slot.
    struct PendingGrant
    {
        uint32_t remaining; // bytes of credit that have not been consumed yet
        uint32_t priority;  // ToS value the receiver assigned to this grant
    };

    std::queue<PendingGrant> m_pendingGrants; //!< FIFO of per-grant (credit,priority) pairs
    std::shared_ptr<Stats> m_stats;           //!< Statistics
    uint32_t m_flowId;
    uint32_t m_msgSize;
    uint64_t m_bytesSended;
    uint64_t m_grantedBytes;
    uint64_t m_recvedBytes;
    uint64_t m_uniqueRecvedBytes;

    uint32_t m_unscheduledBytes;
    uint32_t m_rttBytes;
    uint32_t m_unscheduledPrio;
    uint32_t m_scheduledPrio;
    uint32_t m_grantPrio;
};

} // namespace ns3

#endif /* ROCEV2_HOMA_H */
