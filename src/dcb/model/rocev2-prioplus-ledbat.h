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

#ifndef ROCEV2_PRIOPLUS_LEDBAT_H
#define ROCEV2_PRIOPLUS_LEDBAT_H

#include "rocev2-congestion-ops.h"

#include "ns3/data-rate.h"
#include "ns3/queue-size.h"
#include "ns3/random-variable-stream.h"
#include "ns3/rocev2-header.h"
#include "ns3/string.h"

#include <deque>
#include <functional>
#include <map>
#include <vector>

namespace ns3
{

class RoCEv2SocketState;
class PrioplusHeader;


class RoCEv2PrioplusLedbat : public RoCEv2CongestionOps
{
  public:
    /**
     * Get the type ID.
     * \brief Get the type ID.
     * \return the object TypeId
     */
    static TypeId GetTypeId(void);

    RoCEv2PrioplusLedbat();
    RoCEv2PrioplusLedbat(Ptr<RoCEv2SocketState> sockState);
    ~RoCEv2PrioplusLedbat();

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

        // Complete statistics, record per BLS calculation, and only enabled if BLS is enabled
        struct PrioplusDelayCompleteStats
        {
            Time tNow;               //!< Current time
            Time tDelay;             //!< The delay of the packet
            uint32_t cwnd;           //!< The cwnd
            double dIncastAvoidance; //!< The incast avoidance rate
            double aiPart;           //!< The AI part of the rate increase
            double miPart;           //!< The MI part of the rate increase
            double mdPart;           //!< The MD part of the rate decrease
        };

        std::vector<PrioplusDelayCompleteStats>
            vPrioplusCompleteStats; //!< The complete statistics of the
                                   //!< prioplus complete event

        // To avoid copy, use rvalue reference
        void RecordCompleteStats(PrioplusDelayCompleteStats&& stats);

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
    // deque<psn, sendTs, sendRate (in rateRatio)>
    std::deque<PktInfo> m_inflightPkts; //!< sendTs to record RTT.

    Time m_tLastDelay;                 //!< Last delay
    Time m_tLastPktSendTime;           //!< Last packet's send time
    Time m_tLowThreshold;              //!< Low threshold
    Time m_tHighThreshold;             //!< High threshold
    QueueSize m_tLowThresholdInBytes;  //!< Low threshold in bytes
    QueueSize m_tHighThresholdInBytes; //!< High threshold in bytes

    std::map<double, double> m_delayErrorCdf;   //!< CDF of delay error
    Ptr<UniformRandomVariable> m_rngDelayError; //!< rng to choose delay error from the cdf

    double m_raiRatio;
    double m_linearStartRatio;

    // TODO: Terminate the incast avoidance mode
    bool m_incastAvoidance;       //!< Incast avoidance or not
    bool m_isFirstRtt;            //!< Is the first RTT or not
    double m_incastAvoidanceRate; //!< Incast avoidance rate
    Time m_incastAvoidanceTime;   //!< Incast avoidance time
    double m_forgetFactor;        //!< Forget factor for incast avoidance
    uint32_t m_forgetCount;       //!< Forget count for incast avoidance
    bool m_forgetCountEnabled;    //!< Forget count enabled or not

    // Variables for Ledbat
    struct OwdCircBuf
    {
        std::vector<Time> buffer; //!< Vector to store the delay
        uint32_t min;             //!< The index of minimum value
    };

    /**
     * \brief Initialise a new buffer
     *
     * \param buffer The buffer to be initialised
     */
    void InitCircBuf(OwdCircBuf& buffer);

    /// Filter function used by LEDBAT for current delay
    typedef Time (*FilterFunction)(OwdCircBuf&);

    /**
     * \brief Return the minimum delay of the buffer
     *
     * \param b The buffer
     * \return The minimum delay
     */
    static Time MinCircBuf(OwdCircBuf& b);

    /**
     * \brief Return the value of current delay
     *
     * \param filter The filter function
     * \return The current delay
     */
    Time CurrentDelay(FilterFunction filter);

    /**
     * \brief Add new delay to the buffers
     *
     * \param cb The buffer
     * \param owd The new delay
     * \param maxlen The maximum permitted length
     */
    void AddDelay(OwdCircBuf& cb, Time owd, uint32_t maxlen);
    double m_gain;             //!< GAIN value from RFC
    uint32_t m_noiseFilterLen; //!< Length of current delay buffer
    // Time m_lastRollover;       //!< Timestamp of last added delay
    // int32_t m_sndCwndCnt;      //!< The congestion window addition parameter
    OwdCircBuf m_noiseFilter; //!< Buffer to store the current delay
    // uint32_t m_flag;           //!< LEDBAT Flag
    uint32_t m_minCwnd;   //!< Minimum cWnd value mentioned in RFC 6817

    uint32_t m_linearStartBytes; //!< The bytes of linear start
    uint32_t m_aiBytes;          //!< The bytes of AI
    uint32_t m_miBytes;          //!< The bytes of MI
    // uint64_t m_accumulatedDelay; //!< The accumulated delay in the rtt
    // uint32_t m_numDelay;         //!< The number of delay in the rtt
    bool m_secondRtt;           //!< The second RTT or not
    uint32_t m_cwndBeforeTHigh; //!< The cwnd before THigh

    bool m_weighted; //!< Weighted share the bandwidth or not

    bool m_highPriority; //!< Is this flow high priority or not
    bool m_directStart;  //!< Is this flow skip probe before start or not

    bool m_rttBased;      //!< Use RTT based or not
    Time m_rttCorrection; //!< Used to correct delay to RTT

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
}; // class RoCEv2PrioplusLedbat

} // namespace ns3

#endif // ROCEV2_PRIOPLUS_LEDBAT_H
