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

#include "rocev2-credit-cc.h"

#include <map>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

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
    HomaDataTag(uint32_t flowId, uint32_t msgSize);

    void SetFlowId(uint32_t id);
    uint32_t GetFlowId() const;
    void SetMsgSize(uint32_t size);
    uint32_t GetMsgSize() const;

  private:
    uint32_t m_flowId;
    uint32_t m_msgSize;
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
    HomaGrantTag(uint32_t flowId, uint32_t grantOffset, uint8_t priority);

    void SetFlowId(uint32_t id);
    uint32_t GetFlowId() const;
    void SetGrantOffset(uint32_t os);
    uint32_t GetGrantOffset() const;
    void SetPriority(uint8_t p);
    uint8_t GetPriority() const;

  private:
    uint32_t m_flowId;
    uint32_t m_grantOffset;
    uint8_t m_priority;
};

class HomaResendTag : public Tag
{
  public:
    static TypeId GetTypeId(void);
    virtual TypeId GetInstanceTypeId(void) const;
    virtual uint32_t GetSerializedSize(void) const;
    virtual void Serialize(TagBuffer i) const;
    virtual void Deserialize(TagBuffer i);
    virtual void Print(std::ostream& os) const;

    HomaResendTag();
    HomaResendTag(uint32_t flowId, uint32_t offset, uint32_t length);

    void SetFlowId(uint32_t id);
    uint32_t GetFlowId() const;
    void SetOffset(uint32_t os);
    uint32_t GetOffset() const;
    void SetLength(uint32_t len);
    uint32_t GetLength() const;

  private:
    uint32_t m_flowId;
    uint32_t m_offset;
    uint32_t m_length;
};

class HomaScheduler : public Object
{
  public:
    static TypeId GetTypeId();
    HomaScheduler();
    ~HomaScheduler();

    void UpdateFlow(uint32_t flowId, uint32_t msgSize, uint32_t recvedBytes, Ptr<RoCEv2Homa> flow);
    void RemoveFlow(uint32_t flowId);
    void CheckSchedule(uint32_t packetSize, uint32_t rttBytes, uint32_t overcommitLevel);
    uint32_t GetActiveFlowCount() const;

  private:
    struct FlowState
    {
        uint32_t msgSize;
        uint32_t recvedBytes;
        uint32_t grantedBytes;
        Time lastUpdate;
        Ptr<RoCEv2Homa> flow;

        FlowState()
            : msgSize(0),
              recvedBytes(0),
              grantedBytes(0)
        {
        }
    };

    std::map<uint32_t, FlowState> m_activeFlows;
    uint32_t m_overcommitLevel;
    uint32_t m_numScheduledPriorities;
    uint32_t m_numUnscheduledPriorities;
    uint32_t m_rttBytes;
};

class RoCEv2Homa : public RoCEv2CreditCc
{
  public:
    static TypeId GetTypeId();
    RoCEv2Homa();
    RoCEv2Homa(Ptr<RoCEv2SocketState> sockState);
    ~RoCEv2Homa();

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
    void SetReady() override;
    void SetFlowId(uint32_t flowId) override;
    uint32_t GetFlowId() const;

    void UpdateStateSend(Ptr<Packet> packet) override;
    void UpdateStateRecvData(Ptr<Packet> packet, const RoCEv2Header& roce) override;
    void UpdateStateWithRcvACK(Ptr<Packet> packet,
                               const RoCEv2Header& roce,
                               const uint32_t senderNextPSN) override;
    void UpdateStateWithOutbandPkt(Ptr<Packet> packet,
                                   const RoCEv2Header& roce,
                                   const uint32_t senderNextPSN) override;

    void SendGrantACK(uint32_t grantOffset, uint8_t priority);
    void SendResend(uint32_t offset, uint32_t length);

  private:
    Ptr<HomaScheduler> GetNodeScheduler();
    std::shared_ptr<Stats> m_stats; //!< Statistics
    uint32_t m_flowId;
    uint32_t m_msgSize;
    uint64_t m_bytesSended;
    uint64_t m_recvedBytes;

    uint32_t m_unscheduledBytes;
    uint32_t m_unscheduledPrio;
    uint32_t m_scheduledPrio;

    // Homa Design
    uint32_t m_rttBytes;
    uint32_t m_overcommitLevel;
    bool m_isIncast;
    uint32_t m_outstandingRpcThreshold;
    EventId m_resendEvent;
    Time m_resendTimeout;

    void ProcessResend();

    static std::map<uint32_t, Ptr<HomaScheduler>> m_nodeSchedulers;
};

} // namespace ns3

#endif /* ROCEV2_HOMA_H */
