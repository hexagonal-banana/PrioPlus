/*
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

#ifndef DCB_NET_DEVICE_H
#define DCB_NET_DEVICE_H

#include "pausable-queue-disc.h"

#include "ns3/data-rate.h"
#include "ns3/net-device.h"
#include "ns3/node.h"
#include "ns3/traced-callback.h"

namespace ns3
{

// template <typename Item> class Queue;
class ErrorModel;
class DcbChannel;

/**
 * DcbNetDevice is used to provide Data Center Bridging (IEEE 802.1) functions.
 * Currently support Priority-based Flow Control (IEEE 802.1Qbb)
 */
class DcbNetDevice : public NetDevice
{
  public:
    /**
     * \brief Set the TypeId
     *
     * \return The TypeId for this class
     */
    static TypeId GetTypeId(void);

    /**
     * Construct a DcbNetDevice
     */
    DcbNetDevice();

    /**
     * Destroy a DcbNetDevice
     */
    virtual ~DcbNetDevice();

    /**
     * Receive a packet from a connected DcbChannel.
     *
     * The DcbNetDevice receives packets from its connected channel
     * and forwards them up the protocol stack.  This is the public method
     * used by the channel to indicate that the last bit of a packet has
     * arrived at the device.
     *
     * \param packet Ptr to the received packet.
     */
    void Receive(Ptr<Packet> packet);

    virtual void SetReceiveCallback(NetDevice::ReceiveCallback cb) override;

    virtual bool Send(Ptr<Packet> packet, const Address& dest, uint16_t protocolNumber) override;
    virtual bool SendFrom(Ptr<Packet> packet,
                          const Address& source,
                          const Address& dest,
                          uint16_t protocolNumber) override;

    /**
     * Attach the device to a channel.
     *
     * \param ch Ptr to the channel to which this object is being attached.
     * \return true if the operation was successful (always true actually)
     */
    bool Attach(Ptr<DcbChannel> ch);

    /**
     * Attach a queue to the DcbNetDevice.
     *
     * The DcbNetDevice "owns" a queue that implements a queueing
     * method such as DropTailQueue or RedQueue
     *
     * \param queue Ptr to the new queue.
     */
    void SetQueue(Ptr<Queue<Packet>> queue);

    virtual bool SetMtu(const uint16_t mtu) override;
    virtual uint16_t GetMtu(void) const override;

    virtual Ptr<Node> GetNode(void) const override;
    virtual void SetNode(Ptr<Node> node) override;

    /**
     * \param index ifIndex of the device
     */
    virtual void SetIfIndex(const uint32_t index) override;
    /**
     * \return index ifIndex of the device
     */
    virtual uint32_t GetIfIndex(void) const override;

    /**
     * \return the channel this NetDevice is connected to. The value
     *         returned can be zero if the NetDevice is not yet connected
     *         to any channel or if the underlying NetDevice has no
     *         concept of a channel. i.e., callers _must_ check for zero
     *         and be ready to handle it.
     */
    virtual Ptr<Channel> GetChannel(void) const override;

    virtual void SetAddress(Address address) override;

    virtual Address GetAddress(void) const override;

    virtual bool IsLinkUp(void) const override;

    virtual bool IsBroadcast(void) const override;
    virtual Address GetBroadcast(void) const override;

    virtual bool IsMulticast(void) const override;
    virtual Address GetMulticast(Ipv4Address multicastGroup) const override;
    virtual Address GetMulticast(Ipv6Address addr) const override;

    virtual bool IsPointToPoint(void) const override;
    virtual bool IsBridge(void) const override;

    /**
     * \returns true if ARP is needed, false otherwise.
     *
     * Called by higher-layers to check if this NetDevice requires
     * ARP to be used.
     */
    virtual bool NeedsArp(void) const override;

    virtual void AddLinkChangeCallback(Callback<void> callback) override;

    virtual void SetPromiscReceiveCallback(PromiscReceiveCallback cb) override;

    virtual bool SupportsSendFrom(void) const override;

    DataRate GetDataRate() const;
    void ConfigureDataRate(DataRate bps) { SetDataRate(bps); }

    void SetQueueDisc(Ptr<PausableQueueDisc> queueDisc);
    Ptr<PausableQueueDisc> GetQueueDisc() const;
    void SetFcEnabled(bool enabled);

    /**
     * \returns the address of the remote device connected to this device
     * through the point to point channel.
     */
    Address GetRemote(void) const;

    /**
     * \brief Assign operator
     *
     * The method is DISABLED.
     *
     * \param o Other NetDevice
     * \return New instance of the NetDevice
     */
    DcbNetDevice& operator=(const DcbNetDevice& o) = delete;

    /**
     * \brief Copy constructor
     *
     * The method is DISABLED.
     *
     * \param o Other NetDevice
     */
    DcbNetDevice(const DcbNetDevice& o) = delete;

  private:
    /**
     * \brief Dispose of the object
     */
    virtual void DoDispose(void) override;

    /**
     * Start Sending a Packet Down the Wire.
     *
     * The TransmitStart method is the method that is used internally in the
     * PointToPointNetDevice to begin the process of sending a packet out on
     * the channel.  The corresponding method is called on the channel to let
     * it know that the physical device this class represents has virtually
     * started sending signals.  An event is scheduled for the time at which
     * the bits have been completely transmitted.
     *
     * \see PointToPointChannel::TransmitStart ()
     * \see TransmitComplete()
     * \param p a reference to the packet to send
     * \returns true if success, false on failure
     */
    bool TransmitStart(Ptr<Packet> packet);

    /**
     * Stop Sending a Packet Down the Wire and Begin the Interframe Gap.
     *
     * The TransmitComplete method is used internally to finish the process
     * of sending a packet out on the channel.
     */
    void TransmitComplete(void);

    /**
     * Set the Data Rate used for transmission of packets.  The data rate is
     * set in the Attach () method from the corresponding field in the channel
     * to which the device is attached.  It can be overridden using this method.
     *
     * \param bps the data rate at which this object operates
     */
    void SetDataRate(DataRate bps);

    /**
     * Set the interframe gap used to separate packets.  The interframe gap
     * defines the minimum space required between packets sent by this device.
     *
     * \param t the interframe gap time
     */
    void SetInterframeGap(Time t);

    /**
     * Adds the Ethernet headers and trailers to a packet of data in order to
     * respect the protocol implemented by the agent.
     * \param p packet
     * \param protocolNumber protocol number
     */
    void AddEthernetHeader(Ptr<Packet> p, uint16_t protocolNumber);

    std::pair<uint32_t, uint32_t> GetNodeAndPortId() const;

    virtual void NotifyLinkUp(void);

  private:
    /**
     * Enumeration of the states of the transmit machine of the net device.
     */
    enum TxMachineState
    {
        READY, /**< The transmitter is ready to begin transmission of a packet */
        BUSY   /**< The transmitter is busy transmitting a packet */
    };

    Ptr<Node> m_node;       //!< Node owning this NetDevice
    Mac48Address m_address; //!< Mac48Address of this NetDevice
    uint32_t m_ifIndex;     //!< Index of the interface
    bool m_linkUp;          //<! Identify if the link is up or not

    /**
     * The data rate that the Net Device uses to simulate packet transmission
     * timing.
     */
    DataRate m_bps;

    static const uint16_t DEFAULT_MTU = 1500; //!< Default MTU

    /**
     * \brief The Maximum Transmission Unit
     *
     * This corresponds to the maximum
     * number of bytes that can be transmitted as seen from higher layers.
     * This corresponds to the 1500 byte MTU size often seen on IP over
     * Ethernet.
     */
    uint32_t m_mtu;

    /**
     * The interframe gap that the Net Device uses to throttle packet
     * transmission
     */
    Time m_tInterframeGap;

    Ptr<DcbChannel> m_channel;

    /**
     * The Queue which this PointToPointNetDevice uses as a packet source.
     * Management of this Queue has been delegated to the PointToPointNetDevice
     * and it has the responsibility for deletion.
     * \see class DropTailQueue
     */
    Ptr<Queue<Packet>> m_queue;

    /**
     * Whether this device support PFC functions
     */
    bool m_fcEnabled;

    // The QueueDisc correspond to this device
    Ptr<PausableQueueDisc> m_queueDisc;

    /**
     * Error model for receive packet events
     */
    Ptr<ErrorModel> m_receiveErrorModel;

    /**
     * The state of the Net Device transmit state machine.
     */
    TxMachineState m_txMachineState;

    /**
     * The trace source fired when packets come into the "top" of the device
     * at the L3/L2 transition, before being queued for transmission.
     */
    TracedCallback<Ptr<const Packet>> m_macTxTrace;

    /**
     * The trace source fired when packets coming into the "top" of the device
     * at the L3/L2 transition are dropped before being queued for transmission.
     */
    TracedCallback<Ptr<const Packet>> m_macTxDropTrace;

    /**
     * The trace source fired for packets successfully received by the device
     * immediately before being forwarded up to higher layers (at the L2/L3
     * transition).  This is a non-promiscuous trace (which doesn't mean a lot
     * here in the point-to-point device).
     */
    TracedCallback<Ptr<const Packet>> m_macRxTrace;

    /**
     * The trace source fired for packets successfully received by the device
     * but are dropped before being forwarded up to higher layers (at the L2/L3
     * transition).
     */
    TracedCallback<Ptr<const Packet>> m_macRxDropTrace;

    /**
     * The trace source fired when a packet begins the reception process from
     * the medium -- when the simulated first bit(s) arrive.
     */
    TracedCallback<Ptr<const Packet>> m_phyRxBeginTrace;

    /**
     * The trace source fired when a packet ends the reception process from
     * the medium.
     */
    TracedCallback<Ptr<const Packet>> m_phyRxEndTrace;

    /**
     * The trace source fired when the phy layer drops a packet it has received.
     * This happens if the receiver is not enabled or the error model is active
     * and indicates that the packet is corrupt.
     */
    TracedCallback<Ptr<const Packet>> m_phyRxDropTrace;

    /**
     * The trace source fired when a packet begins the transmission process on
     * the medium.
     */
    TracedCallback<Ptr<const Packet>> m_phyTxBeginTrace;

    /**
     * The trace source fired when a packet begins the transmission process on
     * the medium. The node id and port id is also passed as a parameter.
     */
    TracedCallback<Ptr<const Packet>, std::pair<uint32_t, uint32_t>> m_phyTxBeginWithIdTrace;

    /**
     * The trace source fired when a packet ends the transmission process on
     * the medium.
     */
    TracedCallback<Ptr<const Packet>> m_phyTxEndTrace;

    /**
     * The trace source fired when the phy layer drops a packet before it tries
     * to transmit it.
     */
    TracedCallback<Ptr<const Packet>> m_phyTxDropTrace;

    /**
     * A trace source that emulates a non-promiscuous protocol sniffer connected
     * to the device.  Unlike your average everyday sniffer, this trace source
     * will not fire on PACKET_OTHERHOST events.
     *
     * On the transmit size, this trace hook will fire after a packet is dequeued
     * from the device queue for transmission.  In Linux, for example, this would
     * correspond to the point just before a device \c hard_start_xmit where
     * \c dev_queue_xmit_nit is called to dispatch the packet to the PF_PACKET
     * ETH_P_ALL handlers.
     *
     * On the receive side, this trace hook will fire when a packet is received,
     * just before the receive callback is executed.  In Linux, for example,
     * this would correspond to the point at which the packet is dispatched to
     * packet sniffers in \c netif_receive_skb.
     */
    TracedCallback<Ptr<const Packet>> m_snifferTrace;

    NetDevice::ReceiveCallback m_rxCallback; //!< Receive callback

    Ptr<Packet> m_currentPkt; //!< Current packet processed
};

} // namespace ns3

#endif // DCB_NET_DEVICE_H
