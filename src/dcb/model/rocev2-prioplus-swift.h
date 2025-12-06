/*
 * Copyright (c) 2023
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
 * Author: Zhaochen Zhang (zhaochen.zhang@outlook.com)
 */

#ifndef ROCEV2_PRIOPLUS_SWIFT_H
#define ROCEV2_PRIOPLUS_SWIFT_H

#include "rocev2-congestion-ops.h"

#include "ns3/data-rate.h"
#include "ns3/queue-size.h"
#include "ns3/random-variable-stream.h"
#include "ns3/rocev2-header.h"
#include "ns3/string.h"

#include <deque>
#include <map>
#include <vector>

namespace ns3
{

class RoCEv2SocketState;
class PrioplusHeader;

class RoCEv2PrioplusSwift : public RoCEv2CongestionOps
{
  public:
    /**
     * Get the type ID.
     * \brief Get the type ID.
     * \return the object TypeId
     */
    static TypeId GetTypeId(void);

    RoCEv2PrioplusSwift();
    RoCEv2PrioplusSwift(Ptr<RoCEv2SocketState> sockState);
    ~RoCEv2PrioplusSwift();

    // void SetRateAIRatio(double ratio);
    // void SetRateHyperAIRatio(double ratio);

    /**
     * After configuring the Timely, call this function.
     */
    void SetReady() override;

    /**
     * When the sender sending out a packet, add a SeqTsHeader into packet, in order to store
     * timestamp.
     */
    void UpdateStateSend(Ptr<Packet> packet) override;

    /**
     * When the receiver generating an ACK, move the SeqTsHeade from packet to ack.
     */
    void UpdateStateWithGenACK(Ptr<Packet> packet, Ptr<Packet> ack) override;

    /**
     * When the sender receiving an ACK.
     */
    void UpdateStateWithRcvACK(Ptr<Packet> ack,
                               const RoCEv2Header& roce,
                               const uint32_t senderNextPSN) override;

    /**
     * \brief When RTO timer expires, update the state if needed.
     */
    virtual void UpdateStateWithRto() override;

    std::string GetName() const override;

    /**
     * When the sender receiving an probe packet's ACK.
     * This function will be passed to RoCEv2Socket as a Callback.
     */
    void UpdateStateWithOutbandPkt(Ptr<Packet> probe,
                                   const RoCEv2Header& roce,
                                   uint32_t senderNextPSN) override;

    /**
     * \brief Set send pending data callback
     */
    void SetSendPendingDataCb(Callback<void> sendCb);

    class Stats : public RoCEv2CongestionOps::Stats
    {
      public:
        // constructor
        Stats();

        // Detailed statistics, only enabled if needed
        bool bDetailedSenderStats;

        // Complete statistics
        struct PrioplusSwiftCompleteStats
        {
            Time tNow;               //!< Current time
            Time tDelay;             //!< The delay of the packet
            uint32_t cwnd;           //!< The cwnd
            double dIncastAvoidance; //!< The incast avoidance rate
            double aiPart;           //!< The AI part of the rate increase
            double miPart;           //!< The MI part of the rate increase
            double mdPart;           //!< The MD part of the rate decrease
        };

        std::vector<PrioplusSwiftCompleteStats>
            vPrioplusCompleteStats; //!< The complete statistics of the
                                    //!< Prioplus complete event

        // To avoid copy, use rvalue reference
        void RecordCompleteStats(PrioplusSwiftCompleteStats&& stats);

        // Collect the statistics and check if the statistics is correct
        void CollectAndCheck();

        // No getter for simplicity
    };

    std::shared_ptr<RoCEv2CongestionOps::Stats> GetStats() const override;

  private:
    /**
     * Initialize the state.
     */
    void Init();

    // <psn, sendTs, sendRate (in rateRatio)>
    typedef std::tuple<uint32_t, uint64_t, double> PktInfo;

    /**
     * \brief Do rate update with a scale factor
     */
    void DoRateUpdate(double refenceRate,
                      Time delay,
                      double scaleFactor,
                      bool shouldAi,
                      uint32_t& curRateRatio);

    void ResetRttRecord();

    /**
     * \brief Set Tlow and Thigh in bytes
     */
    void SetTlowInBytes(StringValue tlow);
    void SetThighInBytes(StringValue thigh);
    Time ConvertBytesToTime(QueueSize bytes);

    /**
     * \brief Set rate ratio, overwrite the function in RoCEv2CongestionOps
     */
    // void SetRateRatio(double rateRatio);
    void SetCwnd(uint32_t cwnd);

    std::shared_ptr<Stats> m_stats; //!< Statistics

    uint32_t m_lastUpdateSeq; //!< lastUpdateSeq to record RTT.

    enum RttRecordStructure
    {
        DEQUE,
        MAP
    };

    // deque<psn, sendTs, sendRate (in rateRatio)>
    std::deque<PktInfo> m_inflightPkts;            //!< sendTs to record RTT.
    std::map<uint32_t, PktInfo> m_inflightPktsMap; //!< sendTs to record RTT.
    RttRecordStructure m_rttRecordStructure;       //!< The structure to record RTT.

    Time m_tLastDelay;                 //!< Last delay
    Time m_tLastPktSendTime;           //!< Last packet's send time
    Time m_tLowThreshold;              //!< Low threshold
    Time m_tHighThreshold;             //!< High threshold
    QueueSize m_tLowThresholdInBytes;  //!< Low threshold in bytes
    QueueSize m_tHighThresholdInBytes; //!< High threshold in bytes

    double m_raiRatio;
    double m_linearStartRatio;

    bool m_incastAvoidance;       //!< Incast avoidance or not
    bool m_isFirstRtt;            //!< Is the first RTT or not
    double m_incastAvoidanceRate; //!< Incast avoidance rate
    Time m_incastAvoidanceTime;   //!< Incast avoidance time
    double m_forgetFactor;        //!< Forget factor for incast avoidance
    uint32_t m_forgetCount;       //!< Forget count for incast avoidance
    bool m_forgetCountEnabled;    //!< Forget count enabled or not

    // Variables for Swift
    double m_mdFactor;    //!< β in paper. Multiplicative Decrement factor
    double m_maxMdFactor; //!< max_mdf in paper. Maximum Multiplicative Decrement factor
    Time m_tLastDecrease; //!< Last time to decrease the rate

    uint32_t m_linearStartBytes; //!< The bytes of linear start
    uint32_t m_aiBytes;          //!< The bytes of AI
    uint32_t m_miBytes;          //!< The bytes of MI
    bool m_secondRtt;            //!< The second RTT or not
    uint32_t m_cwndBeforeTHigh;  //!< The cwnd before THigh

    bool m_highPriority; //!< Is this flow high priority or not
    bool m_directStart;  //!< Is this flow skip probe before start or not

    bool m_rttBased;      //!< Use RTT based or not
    Time m_rttCorrection; //!< Used to correct delay to RTT

    // Variables for ablation study
    double m_tlowAiStep; //!< The AI step when the delay is lower than tlow, 0 for disable
    bool m_dualRttMi;    //!< Use dual RTT for MI or not

    // Variables for error tolerance
    std::string m_delayErrorFile;                 //!< The file to set the priority config
    Ptr<EmpiricalRandomVariable> m_rngDelayError; //!< rng to choose delay error from the cdf
    bool m_delayErrorEnabled;                     //!< Delay error enabled or not
    double m_delayErrorScale;                     //!< Delay error scale
    void ReadDelayErrorCdf();
    void IncludeDelayError(Time& delay);

    uint32_t m_exceedCount; //!< The count of delay exceed the THigh
    uint32_t m_exceedLimit; //!< The limit of delay exceed the THigh

    // Variables for better threshold setting
    QueueSize m_tChannelWidthBytes;    //!< The width of the channel
    QueueSize m_tChannelIntervalBytes; //!< The interval of the channel
    uint32_t m_priorityNum;            //!< The number of priority in the network
    uint32_t m_priorityIndex;          //!< The index of the priority of this flow, lesser is higher
    void SetChannelWidth(StringValue width);
    void SetChannelInterval(StringValue interval);
    void SetChannelThres();
    std::string m_priorityConfigFile; //!< The file to set the priority config
    void SetPriorityConfig();

    /***** Utility for Probe Packet *****/
    /**
     * \brief Stop sending data and start sending probe packet.
     * This function will be called when:
     * 1. The start of the flow.
     * 2. The delay higher than thigh.
     */
    void StopSendingAndStartProbe(Time delay);

    /**
     * \brief Send a probe packet.
     */
    void SendProbePacket();

    /**
     * \brief Schedule a probe packet.
     *
     * \param delay The oneway delay observed when the probe packet is schedule.
     */
    void ScheduleProbePacket(Time delay);

    uint32_t m_probeSeq;                           //!< The sequence number of the probe packet
    Time m_probeInterval;                          //!< The interval between two probe packets
    EventId m_probeEvent;                          //!< The event to send probe packet
    std::map<uint32_t, uint64_t> m_inflightProbes; //!< <psn, sendTs>
    Ptr<UniformRandomVariable> m_rngProbeTime;     //!< rng to choose probe time

    /**
     * \brief The SendPendingPacket() call back, called every time transact from probe to can send
     * packet to the network.
     */
    Callback<void> m_sendPendingDataCb;
}; // class RoCEv2PrioplusSwift

class PrioplusHeader : public Header
{
    // PrioPlus Header with only one Timestamp
  public:
    PrioplusHeader()
        : m_ts(Simulator::Now().GetNanoSeconds())
    {
    }

    static TypeId GetTypeId(void)
    {
        static TypeId tid = TypeId("ns3::PrioplusHeader")
                                .SetParent<Header>()
                                .SetGroupName("Dcb")
                                .AddConstructor<PrioplusHeader>();
        return tid;
    }

    TypeId GetInstanceTypeId(void) const override
    {
        return GetTypeId();
    }

    inline uint32_t GetSerializedSize(void) const override
    {
        return 8;
    }

    void Serialize(Buffer::Iterator start) const override
    {
        Buffer::Iterator i = start;
        i.WriteHtonU64(m_ts);
    }

    uint32_t Deserialize(Buffer::Iterator start) override
    {
        Buffer::Iterator i = start;
        m_ts = i.ReadNtohU64();
        return GetSerializedSize();
    }

    void Print(std::ostream& os) const override
    {
        os << "time=" << NanoSeconds(m_ts).As(Time::S) << "";
    }

    Time GetTs() const
    {
        return NanoSeconds(m_ts);
    }

    uint64_t m_ts; //!< Timestamp
};

} // namespace ns3

#endif // ROCEV2_PRIOPLUS_SWIFT_H
