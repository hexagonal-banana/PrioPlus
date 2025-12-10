#include "rocev2-expresspass.h"
#include "ns3/socket.h"

#include "ns3/log.h"
#include "ns3/packet.h"
#include "ns3/simulator.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("RoCEv2ExpressPass");

    NS_OBJECT_ENSURE_REGISTERED(RoCEv2ExpressPass);


    TypeId
    RoCEv2ExpressPass::GetTypeId()
    {
        static TypeId tid = TypeId("ns3::RoCEv2ExpressPass")
                                .SetParent<RoCEv2CreditCc>()
                                .AddConstructor<RoCEv2ExpressPass>()
                                .SetGroupName("DCB")
                                .AddAttribute("CreditPrio",
                                              "The priority of the credit request packet.",
                                              UintegerValue(0),
                                              MakeUintegerAccessor(&RoCEv2ExpressPass::m_creditPrio),
                                              MakeUintegerChecker<uint32_t>())
                                .AddAttribute("DataPrio",
                                              "The priority of the data packet.",
                                              UintegerValue(1),
                                              MakeUintegerAccessor(&RoCEv2ExpressPass::m_dataPrio),
                                              MakeUintegerChecker<uint32_t>())
                                ;
        return tid;
    }

    RoCEv2ExpressPass::RoCEv2ExpressPass()
    {
        NS_LOG_FUNCTION(this);
        Init();
    }

    RoCEv2ExpressPass::RoCEv2ExpressPass(Ptr<RoCEv2SocketState> sockState)
    {
        NS_LOG_FUNCTION(this);
        Init();
    }

    RoCEv2ExpressPass::~RoCEv2ExpressPass()
    {
        NS_LOG_FUNCTION(this);
    }

    void
    RoCEv2ExpressPass::Init()
    {
        NS_LOG_FUNCTION(this);
        RegisterCongestionType(GetTypeId());
    }

    void
    RoCEv2ExpressPass::UpdateStateSend(Ptr<Packet> packet)
    {
        NS_LOG_FUNCTION(this << packet);

        SocketIpTosTag ipTosTag;
        ipTosTag.SetTos(m_dataPrio);
        packet->AddPacketTag(ipTosTag);

        RoCEv2CreditCc::UpdateStateSend(packet);
    }

    void
    RoCEv2ExpressPass::SendCreditRequest(Time rto)
    {
        NS_LOG_FUNCTION(this << rto);
        RoCEv2CreditCc::ScheduleNextCreditReq(rto);
    }
}