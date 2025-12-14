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

#ifndef DCB_TRAFFIC_CONTROL_H
#define DCB_TRAFFIC_CONTROL_H

#include "dcb-flow-control-mmu-queue.h"
#include "dcb-flow-control-port.h"
#include "dcb-net-device.h"

#include "ns3/drop-tail-queue.h"
#include "ns3/ipv4-queue-disc-item.h"
#include "ns3/net-device.h"
#include "ns3/pfc-frame.h"
#include "ns3/tag-buffer.h"
#include "ns3/traffic-control-layer.h"
#include "ns3/node.h"

#include <vector>

namespace ns3
{

class Packet;
class QueueDisc;
class NetDeviceQueueInterface;
class DcbFlowControlPort;

/**
 * \defgroup dcb
 *
 * Inherit from Traffic Control layer aims at introducing an equivalent of the Linux Traffic
 * Control infrastructure into ns-3. The Traffic Control layer sits in between
 * the NetDevices (L2) and any network protocol (e.g., IP). It is in charge of
 * processing packets and performing actions on them: scheduling, dropping,
 * marking, policing, etc.
 *
 * \ingroup traffic-control
 *
 * \brief Traffic control layer class
 *
 * This object represents the main interface of the Traffic Control Module.
 * Basically, we manage both IN and OUT directions (sometimes called RX and TX,
 * respectively). The OUT direction is easy to follow, since it involves
 * direct calls: upper layer (e.g. IP) calls the Send method on an instance of
 * this class, which then calls the Enqueue method of the QueueDisc associated
 * with the device. The Dequeue method of the QueueDisc finally calls the Send
 * method of the NetDevice.
 *
 * The IN direction uses a little trick to reduce dependencies between modules.
 * In simple words, we use Callbacks to connect upper layer (which should register
 * their Receive callback through RegisterProtocolHandler) and NetDevices.
 *
 * An example of the IN connection between this layer and IP layer is the following:
 *\verbatim
  Ptr<TrafficControlLayer> tc = m_node->GetObject<TrafficControlLayer> ();

  NS_ASSERT (tc != 0);

  m_node->RegisterProtocolHandler (MakeCallback (&TrafficControlLayer::Receive, tc),
                                   Ipv4L3Protocol::PROT_NUMBER, device);
  m_node->RegisterProtocolHandler (MakeCallback (&TrafficControlLayer::Receive, tc),
                                   ArpL3Protocol::PROT_NUMBER, device);

  tc->RegisterProtocolHandler (MakeCallback (&Ipv4L3Protocol::Receive, this),
                               Ipv4L3Protocol::PROT_NUMBER, device);
  tc->RegisterProtocolHandler (MakeCallback (&ArpL3Protocol::Receive, PeekPointer
 (GetObject<ArpL3Protocol> ())), ArpL3Protocol::PROT_NUMBER, device); \endverbatim
 * On the node, for IPv4 and ARP packet, is registered the
 * TrafficControlLayer::Receive callback. At the same time, on the TrafficControlLayer
 * object, is registered the callbacks associated to the upper layers (IPv4 or ARP).
 *
 * When the node receives an IPv4 or ARP packet, it calls the Receive method
 * on TrafficControlLayer, that calls the right upper-layer callback once it
 * finishes the operations on the packet received.
 *
 * Discrimination through callbacks (in other words: what is the right upper-layer
 * callback for this packet?) is done through checks over the device and the
 * protocol number.
 */
class DcbTrafficControl : public TrafficControlLayer
{
  public:
    /**
     * \brief Get the type ID.
     * \return the object TypeId
     */
    static TypeId GetTypeId(void);

    /**
     * \brief Get the type ID for the instance
     * \return the instance TypeId
     */
    virtual TypeId GetInstanceTypeId(void) const override;

    /**
     * \brief Constructor
     */
    DcbTrafficControl();

    virtual ~DcbTrafficControl();

    // Delete copy constructor and assignment operator to avoid misuse
    DcbTrafficControl(const DcbTrafficControl&) = delete;
    DcbTrafficControl& operator=(const DcbTrafficControl&) = delete;

    using TrafficControlLayer::SetRootQueueDiscOnDevice;
    virtual void SetRootQueueDiscOnDevice(Ptr<NetDevice> device, Ptr<QueueDisc> qDisc);

    /**
     * Register NetDevice number for PFC to initiate counters.
     */
    virtual void RegisterDeviceNumber(const uint32_t num);

    void SetBufferSize(uint32_t bytes);

    /**
     * \brief Called by NetDevices, incoming packet
     *
     * After analyses and scheduling, this method will call the right handler
     * to pass the packet up in the stack.
     *
     * \param device network device
     * \param p the packet
     * \param protocol next header value
     * \param from address of the correspondent
     * \param to address of the destination
     * \param packetType type of the packet
     */
    virtual void Receive(Ptr<NetDevice> device,
                         Ptr<const Packet> p,
                         uint16_t protocol,
                         const Address& from,
                         const Address& to,
                         NetDevice::PacketType packetType) override;

    virtual void Send(Ptr<NetDevice> device, Ptr<QueueDiscItem> item) override;

    /**
     * \brief Called after egress queue pops out a packet.
     * For example, it can be used for flow control doing some egress action.
     */
    void EgressProcess(uint32_t port, uint32_t priority, Ptr<Packet> packet);

    // int32_t CompareIngressQueueLength(uint32_t port, uint8_t priority, uint32_t bytes) const;

    void InstallFCToPort(uint32_t portIdx,
                         Ptr<DcbFlowControlPort> fc,
                         std::vector<ns3::Ptr<ns3::DcbFlowControlMmuQueue>> fcMmuQueues);

    /**
     * \brief Peek the priority of from ToS field of IP header.
     */
    static uint8_t PeekPriorityOfPacket(const Ptr<const Packet> packet);

    constexpr static const uint8_t PRIORITY_NUMBER = 64; // at most 64 priorities

    class PortInfo
    {
      public:
        typedef Callback<void, uint32_t, Ptr<Packet>> FCPacketOutCb;

        PortInfo();

        /**
         * \brief Add a calback to this port when a packet from `fromIdx` is going
         * out through this port.
         */
        void AddPacketOutCallback(uint32_t fromIdx, FCPacketOutCb cb);
        /**
         * \brief Call corresponding callbacks when a packet from `fromIdx` is going
         * out through this port.
         */
        void CallFCPacketOutPipeline(uint32_t fromIdx, uint8_t priority, Ptr<Packet> packet);

        inline void SetFC(Ptr<DcbFlowControlPort> fc,
                          std::vector<Ptr<DcbFlowControlMmuQueue>> fcMmuQueues)
        {
            m_fcEnabled = true;
            m_fc = fc;
            m_fcMmuQueues = fcMmuQueues;
        }

        inline bool FcEnabled() const
        {
            return m_fcEnabled;
        }

        inline Ptr<DcbFlowControlPort> GetFC() const
        {
            return m_fc;
        }

        inline Ptr<DcbFlowControlMmuQueue> GetFCMmuQueue(uint32_t priority) const
        {
            if (priority > m_fcMmuQueues.size())
            {
                NS_FATAL_ERROR("Priority is out of range, current priority is "
                               << priority << ", max priority is " << m_fcMmuQueues.size() - 1
                               << ".");
            }
            return m_fcMmuQueues[priority];
        }

        inline uint32_t GetFCMmuQueueSize() const
        {
            return m_fcMmuQueues.size();
        }

      private:
        bool m_fcEnabled;
        Ptr<DcbFlowControlPort> m_fc;
        std::vector<Ptr<DcbFlowControlMmuQueue>> m_fcMmuQueues;
        // a vector of out callbacks, which will be called one by one when packet out
        std::vector<std::pair<uint32_t, FCPacketOutCb>> m_fcPacketOutPipeline; 
    }; // class PortInfo

    // Used to bind traces
    inline std::vector<PortInfo>& GetPorts()
    {
        return m_buffer.GetPorts();
    }

  protected:
    TracedCallback<Ptr<const Packet>> m_bufferOverflowTrace;

  private:
    class Buffer
    {
      public:
        Buffer();
        void SetBufferSpace(uint32_t bytes);
        void RegisterPortNumber(const uint32_t num);
        /**
         * \brief Process when packet received.
         * Returns whether the packet is accomondated into the buffer, false for packet drop.
         */
        bool InPacketProcess(uint32_t inPortIndex,
                             uint32_t inQueuePriority,
                             uint32_t outPortIndex,
                             uint32_t outQueuePriority,
                             uint32_t packetSize);
        void OutPacketProcess(uint32_t inPortIndex,
                              uint32_t inQueuePriority,
                              uint32_t outPortIndex,
                              uint32_t outQueuePriority,
                              uint32_t packetSize);

        inline PortInfo& GetPort(uint32_t portIndex)
        {
            return m_ports[portIndex];
        }

        inline std::vector<PortInfo>& GetPorts()
        {
            return m_ports;
        }

        inline uint32_t GetSize() const
        {
            return m_totalSize;
        }

        /**
         * \brief Only calculate shared size at first time. Then return the value directly, which
         * means each queue's exclusive buffer size should not be changed after configuration.
         */
        uint32_t GetSharedSize();
        uint32_t GetSharedUsed();

        uint32_t GetSharedRemain()
        {
            return GetSharedSize() - GetSharedUsed();
        }

        void SetPortFcMmuBufferCallback(uint32_t portIndex);

      private:
        uint32_t m_totalSize;
        uint32_t m_totalSharedSize;
        bool m_hasCalSharedSize;
        std::vector<PortInfo> m_ports;
    }; // class Buffer

    Buffer m_buffer;
};

class DeviceIndexTag : public Tag
{
  public:
    DeviceIndexTag() = default;

    explicit DeviceIndexTag(uint32_t index);

    void SetIndex(uint32_t index);

    uint32_t GetIndex() const;

    static TypeId GetTypeId(void);

    virtual TypeId GetInstanceTypeId(void) const override;

    virtual uint32_t GetSerializedSize() const override;

    virtual void Serialize(TagBuffer i) const override;

    virtual void Deserialize(TagBuffer i) override;

    virtual void Print(std::ostream& os) const override;

  private:
    uint32_t m_index; //!< the device index carried by the tag

}; // class DevIndexTag

/**
 * \brief Class of Service (priority) tag for PFC priority.
 */
class CoSTag : public Tag
{
  public:
    CoSTag() = default;

    explicit CoSTag(uint8_t cos);

    void SetCoS(uint8_t cos);

    uint8_t GetCoS() const;

    static TypeId GetTypeId(void);

    virtual TypeId GetInstanceTypeId(void) const override;

    virtual uint32_t GetSerializedSize() const override;

    virtual void Serialize(TagBuffer i) const override;

    virtual void Deserialize(TagBuffer i) override;

    virtual void Print(std::ostream& os) const override;

  private:
    uint8_t m_cos; //!< the Class-of-Service carried by the tag

}; // class CoSTag

} // namespace ns3

#endif // DCB_TRAFFIC_CONTROL_H
