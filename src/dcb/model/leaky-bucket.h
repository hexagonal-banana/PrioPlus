#ifndef LEAKY_BUCKET_H
#define LEAKY_BUCKET_H

#include "ns3/data-rate.h"
#include "ns3/event-id.h"
#include "ns3/nstime.h"
#include "ns3/object.h"

namespace ns3
{

class LeakyBucket : public Object
{
  public:
    static TypeId GetTypeId();
    LeakyBucket();
    LeakyBucket(DataRate rate, uint32_t capacity,Callback<void> cb);

    virtual ~LeakyBucket();

    void Pause();
    void Resume();
    bool isFull();

    void Consume(uint32_t bytes);
    bool CanConsume(uint32_t bytes);
    void SetRefillCallback(Callback<void> cb);
  private:
    DataRate m_rate;
    uint32_t m_capacity;      // in bytes
    uint32_t m_availableTokens; // in bytes

    Time m_RefillInterval;
    uint32_t m_RefillAmount; // in bytes
    EventId m_refillEvent;

    Callback<void> m_RefillCallback;
    void Refill();
};
}
#endif
