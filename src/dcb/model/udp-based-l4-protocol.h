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

#ifndef UDP_BASED_L4_PROTOCOL_H
#define UDP_BASED_L4_PROTOCOL_H

#include "ns3/ipv4-address.h"
#include "ns3/ipv4-interface.h"
#include "ns3/object.h"
#include "ns3/packet.h"
#include "ns3/ptr.h"
#include "ns3/udp-l4-protocol.h"

#include <map>

namespace ns3
{

class Node;
class Socket;
class Ipv4EndPointDemux;
class Ipv4EndPoint;
class UdpBasedSocket;
class NetDevice;

class InnerEndPoint;
class InnerEndPointDemux;

/**
 * \ingroup udp
 * \brief Implementation of the UDP protocol
 */
class UdpBasedL4Protocol : public Object
{
  public:
    struct InnerPortInfo
    {
        uint32_t dstPort;
        uint32_t srcPort;
        Ipv4Address srcAddr;
    };
    /**
     * \brief Get the type ID.
     * \return the object TypeId
     */
    static TypeId GetTypeId(void);
    /* see http://www.iana.org/assignments/protocol-numbers */
    constexpr inline static const uint16_t PROT_NUMBER = 0x11;

    UdpBasedL4Protocol();
    virtual ~UdpBasedL4Protocol();

    // Delete copy constructor and assignment operator to avoid misuse
    UdpBasedL4Protocol(const UdpBasedL4Protocol&) = delete;
    UdpBasedL4Protocol& operator=(const UdpBasedL4Protocol&) = delete;

    /**
     * \brief Set node, device, and udp protocol associated with this stack
     */
    void Setup(Ptr<Node> node, Ptr<NetDevice> dev);

    /**
     * \return A smart Socket pointer to a UdpSocket, allocated by this instance
     * of the UDP protocol
     */
    virtual Ptr<Socket> CreateSocket(void);

    virtual InnerEndPoint* Allocate();
    virtual InnerEndPoint* Allocate(uint32_t dport) = 0;

    virtual uint16_t GetProtocolNumber(void) const;
    virtual uint32_t GetInnerProtocolHeaderSize() const = 0;
    virtual uint32_t GetHeaderSize() const = 0;
    virtual uint32_t GetDefaultServicePort() const = 0;

    /**
     * \brief Send a packet via UDP (IPv4)
     * \param packet The packet to send
     * \param saddr The source Ipv4Address
     * \param daddr The destination Ipv4Address
     * \param sport The source port number
     * \param dport The destination port number
     * \param route The route
     */
    void Send(Ptr<Packet> packet,
              Ipv4Address saddr,
              Ipv4Address daddr,
              uint32_t sport,
              uint32_t dport,
              Ptr<Ipv4Route> route);

    void DeAllocate(InnerEndPoint* endPoint);

    Ipv4Address GetLocalAddress() const;

    /**
     * \brief Parse inner header to obtain demux keys.
     *
     * Must fill dstPort (dest QP), srcPort (src QP) and srcAddr (src IP).
     */
    virtual InnerPortInfo ParseInnerPorts(Ptr<Packet> packet,
                                          Ipv4Header header,
                                          uint16_t port,
                                          Ptr<Ipv4Interface> incomingIntf) = 0;

  protected:
    virtual void DoDispose(void) override;

    virtual void FinishSetup(Ipv4EndPoint* const udpEndPoint) = 0;

    /**
     * \brief Logic before sending packet to UDP.
     * For example, adding packet header.
     */
    // virtual void PreSend (Ptr<Packet> packet, Ipv4Address saddr, Ipv4Address daddr,
    // uint32_t sport, uint32_t dport, Ptr<Ipv4Route> route) = 0;

    /**
     * \brief Called by the L3 protocol when it received a packet to pass on to UDP.
     *
     * \param packet the incoming packet
     * \param header the packet's IPv4 header
     * \param port the remote port
     * \param incomingInterface the incoming interface
     */
    void ForwardUp(Ptr<Packet> packet,
                   Ipv4Header header,
                   uint16_t port,
                   Ptr<Ipv4Interface> incomingIntf);

    virtual void Destroy();

    virtual void NotifyNewAggregate() override;

    InnerEndPointDemux* m_innerEndPoints; //!< A list of inner-UDP end points.

    Ptr<Node> m_node; //!< the node this stack is associated with

    std::vector<Ptr<UdpBasedSocket>> m_sockets; //!< list of sockets

  private:
    Ptr<UdpL4Protocol> m_udp; //!< the associated UDP L4 protocol
    Ipv4EndPoint* m_udpEndPoint;

}; // class UdpBasedL4Protocol

class InnerEndPoint
{
  public:
    InnerEndPoint(uint32_t innerPort, uint32_t destPort);
    ~InnerEndPoint();
    void ForwardUp(Ptr<Packet> p,
                   const Ipv4Header& header,
                   uint32_t sport,
                   Ptr<Ipv4Interface> incomingIntf);

    uint32_t GetLocalPort() const;

    uint32_t GetPeerPort() const;
    void SetPeerPort(uint32_t port);

    void SetRxCallback(
        Callback<void, Ptr<Packet>, Ipv4Header, uint32_t, Ptr<Ipv4Interface>> callback);

    void SetRxEnabled(bool enabled);
    bool IsRxEnabled() const;

  private:
    /**
     * \brief The local port.
     */
    uint32_t m_localPort;

    /**
     * \brief The peer port.
     */
    uint32_t m_peerPort;

    /**
     * \brief The RX callback.
     */
    Callback<void, Ptr<Packet>, Ipv4Header, uint32_t, Ptr<Ipv4Interface>> m_rxCallback;

    bool m_rxEnabled;

}; // class InnerEndpoint

/**
 * \brief Manage InnerEndPoints, keep track of used ports.
 */
class InnerEndPointDemux
{
  public:
    InnerEndPointDemux();
    InnerEndPointDemux(uint32_t portFirst, uint32_t portLast);
    ~InnerEndPointDemux();

    InnerEndPoint* Allocate(uint32_t dport);
    InnerEndPoint* Allocate(uint32_t sport, uint32_t dport);
    void DeAllocate(InnerEndPoint* endPoint);

    /**
     * \brief Lookup endpoint by dstQP.
     */
    InnerEndPoint* Lookup(uint32_t innerPort);
    /**
     * \brief Lookup endpoint by (dstQP, srcIP, srcQP).
     */
    InnerEndPoint* Lookup(uint32_t innerPort, Ipv4Address srcAddr, uint32_t srcPort);

    /**
     * \brief Lookup for port local.
     * \param port port to test
     * \return true if a port local is in EndPoints, false otherwise
     */
    bool LookupPortLocal(uint32_t port);

    /**
     * \brief Allocate an ephemeral port.
     * \returns the ephemeral port
     */
    uint32_t AllocateEphemeralPort(void);

    /**
     * \brief Allocate a per-flow endpoint keyed by (dstQP, srcIP, srcQP).
     */
    InnerEndPoint* AllocateForFlow(uint32_t dstPort, Ipv4Address srcAddr, uint32_t srcPort);
    /**
     * \brief Remove a per-flow endpoint keyed by (dstQP, srcIP, srcQP).
     */
    void DeAllocateFlow(uint32_t dstPort, Ipv4Address srcAddr, uint32_t srcPort);

  private:
    struct FlowKey
    {
        uint32_t dstPort;
        Ipv4Address srcAddr;
        uint32_t srcPort;
        bool operator<(const FlowKey& other) const
        {
            if (dstPort != other.dstPort)
            {
                return dstPort < other.dstPort;
            }
            if (srcAddr != other.srcAddr)
            {
                return srcAddr < other.srcAddr;
            }
            return srcPort < other.srcPort;
        }
    };

    // m_endPoints: classic demux keyed only by inner dst port (QP). Used for listener sockets and
    // protocols that do not need per-flow sockets.
    std::map<uint32_t, InnerEndPoint*> m_endPoints;
    // m_flowEndPoints: fine-grained demux keyed by (dstQP, srcIP, srcQP). Used to dispatch packets
    // to per-flow sockets after the listener creates them.
    std::map<FlowKey, InnerEndPoint*> m_flowEndPoints;

    uint32_t m_innerPortFirst; // The minimum port number can be used
    uint32_t m_innerPortLast;  // The maximum port number can be used
    uint32_t m_ephemeral;      // The last allocated ephemeral port

}; // class InnerEndPointDemux

} // namespace ns3

#endif // UDP_BASED_L4_PROTOCOL_H
