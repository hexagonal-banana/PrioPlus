#ifndef EXPRESSPASS_H
#define EXPRESSPASS_H

#include "rocev2-credit-cc.h"
#include <queue>

namespace ns3{

    class RoCEv2SocketState;

    class RoCEv2ExpressPass : public RoCEv2CreditCc
    {
    public:

    static TypeId GetTypeId();
    RoCEv2ExpressPass();
    RoCEv2ExpressPass(Ptr<RoCEv2SocketState> sockState);
    ~RoCEv2ExpressPass() override;

    void UpdateStateSend(Ptr<Packet> packet) override;
    void UpdateStateWithRcvACK(Ptr<Packet> ack,
                            const RoCEv2Header& roce,
                            const uint32_t senderNextPSN) override;
    void StartCreditAckLoop(const RoCEv2Header& roce);
    void UpdateStateRecvData(Ptr<Packet> packet) override;
    void SendCreditRequest(Time rto) ;
    void SendCreditAck(uint32_t psn) ;
    void UpdateStateWithOutbandPkt(Ptr<Packet> packet,
                                   const RoCEv2Header& roce,
                                   const uint32_t senderNextPSN) override;

    std::string GetName() const;

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

    class CreditSeqTag: public Tag{
        public:
        static TypeId GetTypeId();
        CreditSeqTag();
        CreditSeqTag(uint64_t seq);
        TypeId GetInstanceTypeId() const override;
        uint64_t GetSeq() const;
        void SetSeq(uint64_t seq);
        uint32_t GetSerializedSize() const override;
        void Serialize(TagBuffer i) const override;
        void Deserialize(TagBuffer i) override;
        void Print(std::ostream& os) const override;
    private:
        uint64_t m_seq;
    };
    private:
    enum RateControlLastAction
    {
        RATE_CONTROL_LAST_ACTION_INCREASE,
        RATE_CONTROL_LAST_ACTION_DECREASE,
        RATE_CONTROL_LAST_ACTION_NONE,
    };
    std::shared_ptr<Stats> m_stats; //!< Statistics
    void Init();
    void RateControl(double lossRatio);
    uint32_t m_creditPrio;
    uint32_t m_dataPrio;
    DataRate m_creditRate;
    DataRate m_maxCreditRate;
    double m_initCreditRateRatio;
    double m_aggressiveRatio;
    double m_maxAggressiveRatio;
    double m_minAggressiveRatio;
    double m_targetLossRatio;

    
    uint32_t m_creditLossCount;
    std::queue<uint64_t> m_senderCreditSeqList;

    uint64_t m_nextCreditSeq;
    uint64_t m_lastRecvCreditSeq;
    uint64_t m_lastUpadateRateSeq;
    uint64_t m_nextUpdateSeq;
    RateControlLastAction m_rateControlLastAction;
};
}

#endif // EXPRESSPASS_H