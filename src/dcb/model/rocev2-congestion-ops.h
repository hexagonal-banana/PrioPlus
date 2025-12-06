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
#ifndef ROCEV2_CONGESTION_OPS_H
#define ROCEV2_CONGESTION_OPS_H

#include "ns3/data-rate.h"
#include "ns3/object.h"
#include "ns3/packet.h"
#include "ns3/rocev2-header.h"
#include "ns3/timer.h"
#include "ns3/traced-value.h"

#include <map>

namespace ns3
{

class RoCEv2SocketState;

class RoCEv2CongestionOps : public Object
{
  public:
    /**
     * \brief Get the type ID.
     *
     * \return the object TypeId
     */
    static TypeId GetTypeId();

    class Stats
    {
      public:
        // constructor
        Stats();

        // Detailed statistics, only enabled if needed
        bool bDetailedSenderStats;
        std::vector<std::pair<Time, DataRate>> vCcRate; //!< Record the time when the rate changes,
                                                        //!< true for increase, false for decrease

        // Recorder function of the detailed statistics
        void RecordCcRate(DataRate rate);
        // Collect the statistics and check if the statistics is correct
        void CollectAndCheck();

        virtual ~Stats()
        {
        }
    };

    RoCEv2CongestionOps(std::shared_ptr<Stats> stats);
    RoCEv2CongestionOps(Ptr<RoCEv2SocketState> sockState, std::shared_ptr<Stats> stats);
    ~RoCEv2CongestionOps() override;

    void SetStopTime(Time stopTime);

    void SetSockState(Ptr<RoCEv2SocketState> sockState);

    /**
     ********** VIRTUAL FUNCTIONS**********
     * implemented by subclasses.
     */

    /**
     * \brief Get the name of the congestion control algorithm. All subclasses
     * must implement this function.
     *
     * \return A string identifying the name
     */
    virtual std::string GetName() const = 0;

    /**
     * \brief When the sender sending out a packet, update the state if needed.
     *
     * Do nothing in this class. And implementions in subclasses is not necessary.
     *
     * Note: This function has the pointer of the packet to send as parameter, which has the
     * RoCev2Header and is used after calling this function in RoCEv2Socket::DoSendDataPacket. So,
     * the packet should be carefully modified in this.
     */
    virtual void UpdateStateSend(Ptr<Packet> packet)
    {
    }

    /**
     * \brief When receiving a CNP, update the state if needed.
     *
     * Do nothing in this class. And implementions in subclasses is not necessary.
     */
    virtual void UpdateStateWithCNP()
    {
    }

    /**
     * \brief When receiving an ACK, update the state if needed.
     *
     * Do nothing in this class. And implementions in subclasses is not necessary.
     *
     * Note: This function has the pointer of ACK packet as parameter, which is used after calling
     * this function in RoCEv2Socket::ForwardUp. So, the ACK packet should be carefully modified in
     * this.
     */
    virtual void UpdateStateWithRcvACK(Ptr<Packet> ack,
                                       const RoCEv2Header& roce,
                                       const uint32_t senderNextPSN)
    {
    }

    /**
     * \brief When Generating an ACK, update the state if needed.
     *
     * Do nothing in this class. And implementions in subclasses is not necessary.
     *
     * Note: This function has the pointer of packet as parameter, which is used after calling
     * this function in RoCEv2Socket::HandleDataPacket. So, the packet should be carefully modified
     * in this.
     *
     * \param packet The packet received.
     * \param ack The ACK packet to be sent.
     */
    virtual void UpdateStateWithGenACK(Ptr<Packet> packet, Ptr<Packet> ack)
    {
    }

    /**
     * \brief When receiving an out-of-band packet (e.g., probe ACK), update state if needed.
     *
     * Do nothing in this class. Implementations in subclasses are optional.
     */
    virtual void UpdateStateWithOutbandPkt(Ptr<Packet> packet,
                                           const RoCEv2Header& roce,
                                           const uint32_t senderNextPSN)
    {
    }

    /**
     * \brief When RTO timer expires, update the state if needed.
     *
     * Do nothing in this class. And implementions in subclasses is not necessary.
     */
    virtual void UpdateStateWithRto()
    {
    }

    /**
     * \brief When RoCEv2Socket is binded to netdevice and has configured everything about
     * sockState, start some initialization if needed.
     *
     * Do nothing in this class. And implementions in subclasses is not necessary.
     *
     * Note: In subclass's constructor, the sockState is not set yet. So, any initialization with
     * sockState should be done in this function.
     */
    virtual void SetReady()
    {
        SetRateRatio(m_startRateRatio);
    }

    /**
     * \brief Get headersize.
     */
    inline uint32_t GetExtraHeaderSize() const
    {
        return m_extraHeaderSize;
    }

    /**
     * \brief Get the TypeId of congestion control algorithm.
     */
    static TypeId GetCongestionTypeId(uint16_t idx)
    {
        return m_mCongestionTypeIds[idx];
    }

    /**
     * \brief Get extra ACK size.
     */
    inline uint32_t GetExtraAckSize() const
    {
        return m_extraAckSize;
    }

    typedef std::pair<std::string, Ptr<AttributeValue>> CcOpsConfigPair_t;

    virtual std::shared_ptr<Stats> GetStats() const;

  protected:
    std::shared_ptr<Stats> m_stats; //!< Statistics

    /**
     * \return true if current time is not over stopTime.
     */
    bool CheckStopCondition();
    Ptr<RoCEv2SocketState> m_sockState;
    Time m_stopTime;

    uint32_t m_extraHeaderSize;
    uint32_t m_extraAckSize; // if CCops add something to ACK, this should be set.

    /**
     * The map and recording the used congestion control algorithms in this simulation.
     * In the map, the first uint16_t is the uid of the TypeId, and the second TypeId is the TypeId
     * of algorithm. Thus we can get idx from name, and get name from idx in O(1).
     */
    static std::map<uint16_t, TypeId> m_mCongestionTypeIds;

    /**
     * \brief Register the congestion control algorithm.
     * This function should be called in the constructor of subclass.
     */
    inline void RegisterCongestionType(TypeId typeId)
    {
        // The typeId of congestion control algorithm is form GetName().
        // Check if the typeId is already registered.
        if (m_mCongestionTypeIds.find(typeId.GetUid()) == m_mCongestionTypeIds.end())
        {
            // Not registered, register it.
            m_mCongestionTypeIds[typeId.GetUid()] = typeId;
        }
    }

    /**
     ********** WINDOW-BASED **********
     */

    /**
     * \brief This function will set the sockState's cwnd.
     */
    void SetCwnd(uint64_t cwnd);
    bool m_isPacing; //!< if true, the Window-based CC will pace the sending rate.

    inline void SetPacing(bool pacing)
    {
        m_isPacing = pacing;
    }

    /**
     ********** RATE-BASED **********
     */

    /**
     * \brief This function will set the sockState's rateRatio.
     */
    void SetRateRatio(double rateRatio);
    bool m_isLimiting; //!< if true, the Rate-based CC will limit the cwnd.

    inline void SetLimiting(bool limiting)
    {
        m_isLimiting = limiting;
    }

    bool m_isStaticLimiting; //!< if true, the cwnd is set to base BDP.

    double m_startRateRatio; //!< the start rate ratio of Rate-based CC (or Cwnd-based CC if
                             //!< m_isLimiting).
    double m_maxRateRatio;   //!< the max rate ratio of Rate-based CC (or Cwnd-based CC if
                             //!< m_isLimiting).
};

class CongestionTypeTag : public Tag
{
  public:
    /**
     * \brief Get the type ID.
     * \return the object TypeId
     */
    static TypeId GetTypeId();
    TypeId GetInstanceTypeId() const override;
    uint32_t GetSerializedSize() const override;
    void Serialize(TagBuffer buf) const override;
    void Deserialize(TagBuffer buf) override;
    void Print(std::ostream& os) const override;
    CongestionTypeTag();

    /**
     * Constructs a CongestionTypeTag with the given Congestion Type
     */
    CongestionTypeTag(uint16_t congestionType);
    void SetCongestionTypeIdx(uint16_t congestionType);
    uint16_t GetCongestionTypeIdx() const;
    TypeId GetCongestionTypeId() const;

  private:
    uint16_t m_congestionType;
};

} // namespace ns3

#endif
