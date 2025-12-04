#ifndef ROCERV2_CREDIT_SPARY_H
#define ROCERV2_CREDIT_SPARY_H

#include "rocev2-nocc.h"

namespace ns3
{

class RoCEv2SocketState;

class RoCEv2CreditSpary : public RoCEv2Nocc
{
  public:
    /**
     * Get the type ID.
     * \brief Get the type ID.
     * \return the object TypeId
     */
    static TypeId GetTypeId();

    RoCEv2CreditSpary();

    ~RoCEv2CreditSpary() override;

    void UpdateStateWithGenACK(Ptr<Packet> packet, Ptr<Packet> ackPacket) override;
};
} // namespace ns3
#endif // ROCERV2_CREDIT_SPARY_H