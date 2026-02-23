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

#include <map> // 添加map容器支持

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

    class HomaScheduler : public Object
    {
      public:
        static TypeId GetTypeId();
        HomaScheduler();
        ~HomaScheduler();

        void UpdateFlow(uint32_t flowId,
                        uint32_t msgSize,
                        uint32_t recvedBytes,
                        Ptr<RoCEv2Homa> flow);
        void RemoveFlow(uint32_t flowId);
        void AddReadyToSendGrant(uint32_t flowId);
        void CheckSchedule(uint32_t packetSize);

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
        std::vector<uint32_t> m_readyToSendQueue;
        uint32_t m_overcommitLevel;
    };

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

    void SetPacketReceived(uint32_t packetOffset, uint32_t packetSize);
    bool IsPacketReceived(uint32_t packetOffset) const;
    uint32_t GetUniqueReceivedBytes() const;
    void ProcessOutOfOrderBuffer();

  private:
    std::shared_ptr<Stats> m_stats; //!< Statistics
    uint32_t m_flowId;
    uint32_t m_msgSize;
    uint64_t m_bytesSended;
    uint64_t m_recvedBytes;
    uint64_t m_uniqueRecvedBytes;

    uint32_t m_unscheduledBytes;
    uint32_t m_rttBytes; // RTT字节数
    uint32_t m_unscheduledPrio;
    uint32_t m_scheduledPrio;
    uint32_t m_grantPrio;

    // 乱序和丢包处理相关成员变量
    uint32_t m_expectedPsn;                             // 期望的下一个包序列号
    uint32_t m_lostPacketCount;                         // 丢包计数
    std::map<uint32_t, Ptr<Packet>> m_outOfOrderBuffer; // 乱序包缓冲区

    // 重传情况下recvedBytes统计相关成员变量
    std::vector<bool> m_receivedPackets; // 跟踪哪些包偏移量已被接收
    std::vector<uint32_t> m_packetSizes; // 跟踪每个接收包的大小
    std::shared_ptr<HomaScheduler> m_nodeScheduler;

    // static std::map<uint32_t, Ptr<HomaScheduler>> m_nodeSchedulers;
};

} // namespace ns3

#endif /* ROCEV2_HOMA_H */
