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
 * Author: Pavinberg <pavin0702@gmail.com>
 */

#ifndef ROCEV2_L4_PROTOCOL_H
#define ROCEV2_L4_PROTOCOL_H

#include "udp-based-l4-protocol.h"

#include <map>
#include <deque>

namespace ns3
{

class RoCEv2L4Protocol : public UdpBasedL4Protocol
{
  public:
    constexpr inline static const uint16_t PROT_NUMBER = 4791;
    constexpr inline static const uint32_t DEFAULT_DST_QP = 100;

    /**
     * \brief Get the type ID.
     * \return the object TypeId
     */
    static TypeId GetTypeId(void);

    RoCEv2L4Protocol();
    virtual ~RoCEv2L4Protocol();

    // virtual InnerEndPoint *Allocate () override;
    using UdpBasedL4Protocol::Allocate;
    virtual InnerEndPoint* Allocate(uint32_t dstPort) override;
    InnerEndPoint* Allocate(uint32_t srcPort, uint32_t dstPort);

    /**
     * \brief Check if the local port is already used.
     */
    bool CheckLocalPortExist(uint32_t localPort);

    virtual uint16_t GetProtocolNumber(void) const override;

    virtual uint32_t GetInnerProtocolHeaderSize() const override;

    virtual uint32_t GetHeaderSize() const override;

    virtual uint32_t GetDefaultServicePort() const override;
    static uint32_t DefaultServicePort();

    virtual Ptr<Socket> CreateSocket() override;
    /**
     * \brief Parse RoCEv2 header to extract dstQP/srcQP/srcIP.
     */
    virtual InnerPortInfo ParseInnerPorts(Ptr<Packet> packet,
                                          Ipv4Header header,
                                          uint16_t port,
                                          Ptr<Ipv4Interface> incomingIntf) override;

    static Ptr<Packet> GenerateCNP(uint32_t srcQP, uint32_t dstQP);
    static Ptr<Packet> GenerateACK(uint32_t srcQP,
                                   uint32_t dstQP,
                                   uint32_t expectedPSN,
                                   const uint32_t payloadSize = 6);
    static Ptr<Packet> GenerateNACK(uint32_t srcQP,
                                    uint32_t dstQP,
                                    uint32_t expectedPSN,
                                    const uint32_t payloadSize = 6);

    static bool IsCNP(Ptr<Packet> packet);

    /**
     * \brief Get the dst QP from RoCEv2 header.
     * \return The dst QP number.
     */
    /**
     * \brief Allocate per-flow endpoint (dstQP, srcIP, srcQP).
     */
    InnerEndPoint* AllocateFlow(uint32_t dstPort, Ipv4Address srcAddr, uint32_t srcPort);
    /**
     * \brief Lookup per-flow endpoint (dstQP, srcIP, srcQP).
     */
    InnerEndPoint* LookupFlow(uint32_t dstPort, Ipv4Address srcAddr, uint32_t srcPort);
    /**
     * \brief Remove per-flow endpoint (dstQP, srcIP, srcQP).
     */
    void DeAllocateFlow(uint32_t dstPort, Ipv4Address srcAddr, uint32_t srcPort);

    // protected:
    // void ServerReceive (Ptr<Packet> packet, Ipv4Header header, uint32_t port,
    //                     Ptr<Ipv4Interface> incommingInterface);

    /**
     * Flow scheduler for RoCEv2.
     * Objective: schedule the flows sending on this host when contention occurs. The scheduler will
     * get prioritized max-min fairness among flows.
     *
     * When a socket want to send a packet, it calls the RoCEv2L4Proto to check whether it can send.
     * If not, it register a callback to the RoCEv2L4Proto. When the RoCEv2L4Proto want to send a
     * packet, it check the length of the corresponding qdisc and only can send when the length of
     * the qdisc is 0. When qdisc in taffic control layer has no packet to send, it will call the
     * callback function to notify the RoCEv2L4Proto to send a packet down.
     */

    /**
     * \brief Check whether the socket could send a packet to the qdisc corresponding to the
     * piority.
     */
    bool CheckCouldSend(uint32_t portIdx, uint32_t priority) const;

    /**
     * \brief Register the SendPendingData callback function
     */
    void RegisterSendPendingDataCallback(uint32_t portIdx, uint32_t priority, uint32_t innerPrio, Callback<void> cb);

    /**
     * \brief Called when the qdisc corresponding to the priority is empty.
     */
    void NotifyCouldSend(uint32_t portIdx, uint32_t priority);

  private:
    virtual void FinishSetup(Ipv4EndPoint* const udpEndPoint) override;

    // virtual void PreSend (Ptr<Packet> packet, Ipv4Address saddr, Ipv4Address daddr, uint32_t
    // srcQP,
    //                       uint32_t destQP, Ptr<Ipv4Route> route) override;

    std::map<uint32_t, uint32_t> m_qpMapper; //!< map destQP to stcQP, not used for now

    typedef std::deque<Callback<void>> SocketSendCbQueue;
    typedef std::map<uint32_t, SocketSendCbQueue> InnerPrioritySendCbQueue;
    typedef std::map<uint32_t, InnerPrioritySendCbQueue> PrioritySendCbQueue;
    typedef std::map<uint32_t, PrioritySendCbQueue> PortSendCbQueue;

    PortSendCbQueue m_sendCbQueue;

}; // class RoCEv2L4Protocol

} // namespace ns3

#endif // ROCEV2_L4_PROTOCOL_H
