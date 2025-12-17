#include "leaky-bucket.h"

#include "ns3/log.h"
#include "ns3/simulator.h"
#include <algorithm>
#include <iostream>
NS_LOG_COMPONENT_DEFINE("LeakyBucket");

namespace ns3
{
    TypeId
    LeakyBucket::GetTypeId()
    {
        static TypeId tid = TypeId("ns3::LeakyBucket")
                                .SetParent<Object>()
                                .AddConstructor<LeakyBucket>();
        return tid;
    }

    LeakyBucket::LeakyBucket()
        {
        NS_LOG_FUNCTION(this);
        }
    LeakyBucket::LeakyBucket(DataRate rate, uint32_t capacity,Callback<void> cb)
        : m_rate(rate),
          m_capacity(capacity),
          m_availableTokens(capacity),
          m_RefillInterval(NanoSeconds(50)),
          m_RefillCallback(cb)
    {
        NS_LOG_FUNCTION(this << rate << capacity);
        m_RefillAmountbits = static_cast<uint32_t>(m_rate.GetBitRate() * m_RefillInterval.GetSeconds());
        m_RefillCompensatebits=0;
        NS_ASSERT(m_RefillAmountbits<=m_capacity*8);
        m_refillEvent = Simulator::Schedule(m_RefillInterval, &LeakyBucket::Refill, this);
        //m_refillEvent = Simulator::ScheduleNow(&LeakyBucket::Refill, this);
        //Refill();
        }

    LeakyBucket::~LeakyBucket()
    {
        NS_LOG_FUNCTION(this);
        if (!m_refillEvent.IsExpired())
        {
            m_refillEvent.Cancel();
        }
    }

    void LeakyBucket::SetRefillCallback(Callback<void> cb)
    {
        m_RefillCallback = cb;
    }

    void LeakyBucket::Refill()
    {
        NS_LOG_FUNCTION(this);
        //std::cout << "Refill called, available tokens: " << m_availableTokens << std::endl;
        m_RefillCompensatebits+=m_RefillAmountbits%8;
        m_availableTokens = std::min(m_availableTokens + m_RefillAmountbits/8+m_RefillCompensatebits/8, m_capacity);
        m_RefillCompensatebits%=8;
        if (m_refillEvent.IsExpired()&&!isFull())
        {
            m_refillEvent = Simulator::Schedule(m_RefillInterval, &LeakyBucket::Refill, this);
        }
        if (!m_RefillCallback.IsNull())
        {
            m_RefillCallback();
        }
        
    }

    bool LeakyBucket::CanConsume(uint32_t bytes)
    {
        if(m_refillEvent.IsExpired()&&!isFull())
            m_refillEvent = Simulator::Schedule(m_RefillInterval,&LeakyBucket::Refill, this);
        NS_LOG_FUNCTION(this << bytes);
        return m_availableTokens >= bytes;
    }

    // must call CanConsume(bytes) to make sure it returns true before Consume(bytes)
    void LeakyBucket::Consume(uint32_t bytes)
    {
        NS_LOG_FUNCTION(this << bytes);
        NS_ABORT_IF(!CanConsume(bytes));
        m_availableTokens -= bytes;
        return;
    }

    void LeakyBucket::Pause()
    {
        NS_LOG_FUNCTION(this);
        if (!m_refillEvent.IsExpired())
        {
            m_refillEvent.Cancel();
        }
    }
    void LeakyBucket::Resume()
    {
        NS_LOG_FUNCTION(this);
        if (m_refillEvent.IsExpired())
        {
            m_refillEvent = Simulator::Schedule(m_RefillInterval, &LeakyBucket::Refill, this);
        }
    }

    bool LeakyBucket::isFull()
    {
        NS_LOG_FUNCTION(this);
        return m_availableTokens == m_capacity;
    }
}