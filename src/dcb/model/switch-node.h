#ifndef SWITCH_NOTE_H
#define SWITCH_NOTE_H

#include "ns3/ipv4-header.h"
#include "ns3/net-device.h"
#include "ns3/node.h"
#include "ns3/packet.h"
#include "ns3/tcp-header.h"
#include "ns3/udp-header.h"
#include "ns3/random-variable-stream.h"
#include <map>
namespace ns3
{

/**
 * This class implements per-flow ECMP and provides virtual methods for subclasses to process
 * packet. Note: all associated NetDevice MUST be PointToPointNetDevice
 */
class SwitchNode : public Node
{

  public:
    static TypeId GetTypeId();
    SwitchNode();
    void ReceivePacketAfterTc(Ptr<NetDevice> dev,
                              Ptr<const Packet> packet,
                              uint16_t protocol,
                              const Address& from,
                              const Address& to,
                              NetDevice::PacketType packetType);

    /**
     * \brief Inject an IP packet into the switch forwarding pipeline.
     *
     * Used by NdpSwitchQueue::ReturnToSender() to route RTS packets back to
     * the original sender via ECMP routing.  The packet must include the IPv4
     * header; it will be routed according to the destination IP address.
     *
     * \param inDev  The ingress device (used only for PER_PACKET_SYMMETRIC mode).
     * \param packet Packet with IPv4 header prepended.
     */
    void SendIpv4Packet(Ptr<NetDevice> inDev, Ptr<Packet> packet);

    /**
     * \brief Pre-register a deterministic multipath route for this switch.
     *
     * Called during topology configuration (before Simulator::Run).
     * These routes are applied at the end of DoInitialize(), overriding
     * any ECMP entries from GlobalRouter for the same IP key.
     *
     * \param switchId  Node ID of the switch
     * \param ip        Destination IP (exact) or subnet base (e.g. 0x0A010000 for 10.1.0.0)
     * \param devIdx    Egress device index (deterministic, single next-hop)
     */
    static void AddMultipathRoute(uint32_t switchId, uint32_t ip, int devIdx);

    /**
     * \brief Clear all pre-registered multipath routes (for reconfiguration).
     */
    static void ClearMultipathRoutes();

  protected:
    void DoInitialize() override;
    uint32_t GetEgressDevIndex(Ptr<Packet> packet); // returns ECMP calculated egress port
    uint32_t GetEgressDevIndexRandom(Ptr<Packet> packet); // returns random egress port
    virtual void ReceiveIpv4Packet(Ptr<NetDevice> inDev, Ptr<const Packet> packet);

  private:
    bool ReceiveFromDevice(Ptr<NetDevice> device,
                           Ptr<const Packet> packet,
                           uint16_t protocol,
                           const Address& from);

  private:
    std::map<uint32_t, std::vector<int>> m_routeTable;

    /// Pre-registered multipath routes: switchNodeId → [(ip, devIdx), ...]
    static std::map<uint32_t, std::vector<std::pair<uint32_t, int>>> s_multipathRoutes;

    static uint64_t m_randStream;
    Ptr<UniformRandomVariable> m_rand;  

    constexpr static const uint32_t HASH_BUF_SIZE = 12;

    uint32_t m_RoutingMode;
    enum RoutingMode
    {
        PER_PACKET,
        PER_FLOW_ECMP,
        PER_PACKET_SYMMETRIC
    };
    union HashBuf {
        struct
        {
            uint32_t _srcIp;
            uint32_t _dstIp;
            uint16_t _srcPort;
            uint16_t _dstPort;
        } __attribute__((__packed__));

        char _b[HASH_BUF_SIZE];
    };
};

} // namespace ns3
#endif