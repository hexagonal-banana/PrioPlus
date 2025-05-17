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
 * Author: F.Y. Xue <xue.fyang@foxmail.com>
 */

#ifndef ROCEV2_TIMELY_H
#define ROCEV2_TIMELY_H

#include "rocev2-congestion-ops.h"

#include "ns3/data-rate.h"
#include "ns3/rocev2-header.h"

#include <map>

namespace ns3
{

class RoCEv2SocketState;

/**
 * The Timely implementation according to paper:
 *   Radhika Mittal, et al. "TIMELY: RTT-based Congestion Control for the Datacenter." ACM SIGCOMM.
 *   \url https://dl.acm.org/doi/10.1145/2785956.2787510
 */
class RoCEv2Timely : public RoCEv2CongestionOps
{
  public:
    /**
     * Get the type ID.
     * \brief Get the type ID.
     * \return the object TypeId
     */
    static TypeId GetTypeId();

    RoCEv2Timely();
    RoCEv2Timely(Ptr<RoCEv2SocketState> sockState);
    ~RoCEv2Timely() override;

    // void SetRateAIRatio(double ratio);
    // void SetRateHyperAIRatio(double ratio);

    /**
     * After configuring the Timely, call this function.
     */
    void SetReady() override;

    /**
     * When the sender sending out a packet, recover a timeslot into the map.
     */
    void UpdateStateSend(Ptr<Packet> packet) override;

    /**
     * When the sender receiving an ACK, do what Timely does.
     */
    void UpdateStateWithRcvACK(Ptr<Packet> ack,
                               const RoCEv2Header& roce,
                               const uint32_t senderNextPSN) override;

    std::string GetName() const override;

        class Stats : public RoCEv2CongestionOps::Stats
    {
      public:
        // constructor
        Stats();

        // Detailed statistics, only enabled if needed
        bool bDetailedSenderStats;
        std::vector<std::tuple<Time, Time, Time>>
            vPacketDelay; //!< The Delay masurement per packet, recorded as send time, recv time and
                          //!< delay
        std::vector<std::tuple<Time, Time, double>>
            vPacketDelayGradient; //!< The Delay Gradient masurement per packet, recorded as send time,
                                  //!< recv time and delay gradient

        // Recorder function of the detailed statistics
        void RecordPacketDelay(Time sendTs, Time recvTs, Time delay); 
        void RecordPacketDelayGradient(Time sendTs, Time recvTs, double gradient);

        // Collect the statistics and check if the statistics is correct
        void CollectAndCheck();

        // No getter for simplicity
    };

    inline std::shared_ptr<RoCEv2CongestionOps::Stats> GetStats() const override
    {
        return m_stats;
    }

  private:
    /**
     * Initialize the state.
     */
    void Init();

    std::shared_ptr<Stats> m_stats; //!< Statistics
    
    double m_alpha;    //!< α in paper. EWMA weight parameter
    double m_mdFactor; //!< β in paper. Multiplicative Decrement factor

    double m_raiRatio; //!< RateAI / link rate for additive increase

    uint32_t m_incStage; //!< To count how many times that gradient<0

    bool m_haiMode;      //!< whether in hyperactive-increase mode
    uint32_t m_maxStage; //!< N in paper. Default to 5.

    Time m_prevRtt; //!< prev_rtt in paper.
    Time m_rttDiff; //!< rtt_diff in paper.

    Time m_minRtt;                    //!< minRTT in paper.
    Time m_tLow;                      //!< Tlow in paper.
    Time m_tHigh;                     //!< Thigh in paper.
    std::map<uint32_t, Time> m_tsMap; //!< To store timeslot of each PSN.

    uint32_t m_nextUpdateSeq; //!< nextUpdateSeq for controlling updating frequency.
    uint32_t m_perpackets; //!< Update frequency.

    enum UpdateFreq
    {
        PER_RTT,
        PER_SEVERAL_PKTS
    }; //!< Update

    UpdateFreq m_updateFreq; //!< Update frequency.

}; // class RoCEv2Timely

} // namespace ns3

#endif // TIMELY_H
