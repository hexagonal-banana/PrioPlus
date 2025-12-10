#ifndef EXPRESSPASS_H
#define EXPRESSPASS_H

#include "rocev2-credit-cc.h"

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
    void SendCreditRequest(Time rto);
    
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

    private:
    std::shared_ptr<Stats> m_stats; //!< Statistics
    void Init();
    uint32_t m_creditPrio;
    uint32_t m_dataPrio;
};
}

#endif // EXPRESSPASS_H