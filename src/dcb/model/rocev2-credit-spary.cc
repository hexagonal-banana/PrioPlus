#include "rocev2-credit-spary.h"
#include "ns3/log.h"
#include "ns3/ipv4-global-routing.h"
#include "rocev2-congestion-ops.h"

#include "ns3/global-value.h"
#include "ns3/simulator.h"

namespace ns3{;

NS_LOG_COMPONENT_DEFINE("RoCEv2CreditSpary");
NS_OBJECT_ENSURE_REGISTERED(RoCEv2CreditSpary);

TypeId
RoCEv2CreditSpary::GetTypeId()
{
    static TypeId tid = TypeId("ns3::RoCEv2CreditSpary")
                            .SetParent<RoCEv2Nocc>()
                            .SetGroupName("Dcb")
                            .AddConstructor<RoCEv2CreditSpary>();
    return tid;
}
RoCEv2CreditSpary::RoCEv2CreditSpary()
{
    NS_LOG_FUNCTION(this);
    RegisterCongestionType(GetTypeId());
}

RoCEv2CreditSpary::~RoCEv2CreditSpary()
{
    NS_LOG_FUNCTION(this);
}

void RoCEv2CreditSpary::UpdateStateWithGenACK(Ptr<Packet> packet, Ptr<Packet> ackPacket)
{
    NS_LOG_FUNCTION(this << packet << ackPacket);
    PathTag pathTag;
    NS_ASSERT(packet->PeekPacketTag(pathTag));
    pathTag.forward=false;
    ackPacket->AddPacketTag(pathTag);
}
}