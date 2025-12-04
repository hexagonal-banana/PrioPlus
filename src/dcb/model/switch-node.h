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

  protected:
    void DoInitialize() override;
    uint32_t GetEgressDevIndex(Ptr<Packet> packet); // returns ECMP calculated egress port
    uint32_t GetEgressDevIndexRandom(Ptr<Packet> packet); // returns random egress port
    void SendIpv4Packet(Ptr<NetDevice> inDev, Ptr<Packet> packet);
    virtual void ReceiveIpv4Packet(Ptr<NetDevice> inDev, Ptr<const Packet> packet);

  private:
    bool ReceiveFromDevice(Ptr<NetDevice> device,
                           Ptr<const Packet> packet,
                           uint16_t protocol,
                           const Address& from);

  private:
    std::map<uint32_t, std::vector<int>> m_routeTable;

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