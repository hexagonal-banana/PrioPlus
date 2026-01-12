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

#ifndef ROCEV2_CREDIT_CC_H
#define ROCEV2_CREDIT_CC_H

#include "rocev2-congestion-ops.h"

#include "ns3/data-rate.h"
#include "ns3/rocev2-header.h"

#include <functional>
#include <map>
#include <vector>

namespace ns3
{

class RoCEv2SocketState;

class RoCEv2CreditCc : public RoCEv2CongestionOps
{
  public:
    /**
     * Get the type ID.
     * \brief Get the type ID.
     * \return the object TypeId
     */
    static TypeId GetTypeId();

    RoCEv2CreditCc();
    RoCEv2CreditCc(Ptr<RoCEv2SocketState> sockState);
    ~RoCEv2CreditCc() override;

    // void SetRateAIRatio(double ratio);
    // void SetRateHyperAIRatio(double ratio);

    /**
     * After configuring the CC, call this function.
     */
    void SetReady() override;

    /**
     * When the sender sending out a packet, recover a timeslot into the map.
     */
    void UpdateStateSend(Ptr<Packet> packet) override;

    /**
     * When the sender receiving an ACK.
     */
    void UpdateStateWithRcvACK(Ptr<Packet> ack,
                               const RoCEv2Header& roce,
                               const uint32_t senderNextPSN) override;

    /**
     * When receiving out-of-band packet (credit request), start sending credit ACKs.
     */
    void UpdateStateWithOutbandPkt(Ptr<Packet> packet,
                                   const RoCEv2Header& roce,
                                   const uint32_t senderNextPSN) override;

    /**
     * When receiving data packet.
     */
    void UpdateStateRecvData(Ptr<Packet> packet,
                             const RoCEv2Header& roce) override;

    std::string GetName() const override;

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
    std::shared_ptr<Stats> m_stats; //!< Statistics
    
    virtual void StartCreditAckLoop(const RoCEv2Header& roce);
    virtual void SendCreditAck(uint32_t psn);
    virtual void UpdateCreditRate(DataRate creditRate);
    Time ComputeCreditAckInterval(uint32_t ackBytes) const;

    /**
     * \brief Sender sends out CREDIT_REQUEST to request the receiver to send back the Credits.
     * \param rto: the RTO of the CREQ_TimeOut.
     */
    virtual void SendCreditRequest(Time rto);

    /**
     * \brief Schedule a CREDIT_REQUEST packet.
     *
     * \param rto: the RTO of the CREQ_TimeOut.
     */
    virtual void ScheduleNextCreditReq(Time rto);

    EventId m_cReqTimeOut; //!< The event to send credit request again
    EventId m_creditAckEvent; //!< Repeating event to send credit ACKs
    Time m_creditAckInterval;
    uint32_t m_ackPacketSize;
    double m_creditRateRatio;
    uint32_t m_recvAckAfterFinish{0};

    uint32_t m_endPSN{0};
    bool m_recvEndPSN{false};
    private:
    void Init();
}; // class RoCEv2CreditCc

} // namespace ns3

#endif // ROCEV2_CREDIT_CC_H