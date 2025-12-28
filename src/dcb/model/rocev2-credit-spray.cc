#include "rocev2-credit-spray.h"
#include "ns3/socket.h"
#include "rocev2-congestion-ops.h"
#include "rocev2-socket.h"
#include "ns3/log.h"
#include "ns3/packet.h"
#include "ns3/simulator.h"
#include <algorithm>


namespace ns3
{

NS_LOG_COMPONENT_DEFINE("RoCEv2CreditSpray");

NS_OBJECT_ENSURE_REGISTERED(RoCEv2CreditSpray);


    TypeId
    RoCEv2CreditSpray::GetTypeId()
    {
        static TypeId tid = TypeId("ns3::RoCEv2CreditSpray")
                                .SetParent<RoCEv2CreditCc>()
                                .AddConstructor<RoCEv2CreditSpray>()
                                .SetGroupName("DCB")
                                .AddAttribute("CreditPrio",
                                              "The priority of the credit request packet.",
                                              UintegerValue(0),
                                              MakeUintegerAccessor(&RoCEv2CreditSpray::m_creditPrio),
                                              MakeUintegerChecker<uint32_t>())
                                .AddAttribute("DataPrio",
                                              "The priority of the data packet.",
                                              UintegerValue(0x0A), //IpTos transform to prio 2
                                              MakeUintegerAccessor(&RoCEv2CreditSpray::m_dataPrio),
                                              MakeUintegerChecker<uint32_t>())
                                .AddAttribute("MaxAggressiveRatio",
                                              "The MaxAggressiveRatio, 0.5",
                                              DoubleValue(0.5),
                                              MakeDoubleAccessor(&RoCEv2CreditSpray::m_maxAggressiveRatio),
                                              MakeDoubleChecker<double>(0.0, 1.0))
                                .AddAttribute("MinAggressiveRatio",
                                              "The MinAggressiveRatio, 0.01",
                                              DoubleValue(0.01),
                                              MakeDoubleAccessor(&RoCEv2CreditSpray::m_minAggressiveRatio),
                                              MakeDoubleChecker<double>(0.0, 1.0))
                                .AddAttribute("TargetLossRate",
                                              "The target loss rate",
                                              DoubleValue(0.30),
                                              MakeDoubleAccessor(&RoCEv2CreditSpray::m_targetLossRatio),
                                              MakeDoubleChecker<double>(0.0, 1.0))
                                .AddAttribute("InitAggressiveRatio",
                                              "The initial aggressive ratio",
                                              DoubleValue(0.5),
                                              MakeDoubleAccessor(&RoCEv2CreditSpray::m_aggressiveRatio),
                                              MakeDoubleChecker<double>(0.0, 1.0))
                                .AddAttribute("InitCreditRateRatio",
                                              "The initial credit rate ratio to max credit ratio",
                                              DoubleValue(1),
                                              MakeDoubleAccessor(&RoCEv2CreditSpray::m_initCreditRateRatio),
                                              MakeDoubleChecker<double>(0.0, 1.0))
                                ;
        return tid;
    }

    std::string RoCEv2CreditSpray::GetName() const 
    {
        return "RoCEv2CreditSpray";
    }
    RoCEv2CreditSpray::RoCEv2CreditSpray()
        : RoCEv2CreditCc()  
    {
        NS_LOG_FUNCTION(this);
        Init();
    }
    
    RoCEv2CreditSpray::RoCEv2CreditSpray(Ptr<RoCEv2SocketState> sockState)
        : RoCEv2CreditCc(sockState) 
    {
        NS_LOG_FUNCTION(this);
        Init();
    }

    RoCEv2CreditSpray::~RoCEv2CreditSpray()
    {
        NS_LOG_FUNCTION(this);

    }

    void
    RoCEv2CreditSpray::Init()
    {
        NS_LOG_FUNCTION(this);
        RegisterCongestionType(GetTypeId());
    
        m_senderCreditSeqList = std::queue<uint64_t>();
        m_nextCreditSeq = 1;
        m_recvDataCount = 0;
        m_lastUpadateRateSeq = 0;
        m_nextUpdateSeq = 0;
        m_rateControlLastAction = RATE_CONTROL_LAST_ACTION_NONE;
        
        m_senderPathTagList=std::queue<PathTag>();
        m_creditPrio = 0;
        m_dataPrio = 0x0A;
        m_creditRate = DataRate();
        m_maxCreditRate = DataRate();
        m_initCreditRateRatio = 1.0;
        m_aggressiveRatio = 0.5;
        m_maxAggressiveRatio = 0.5;
        m_minAggressiveRatio = 0.01;
        m_targetLossRatio = 0.30;
    }

    void
    RoCEv2CreditSpray::UpdateStateSend(Ptr<Packet> packet)
    {
        NS_LOG_FUNCTION(this << packet);

        NS_ASSERT(!m_senderCreditSeqList.empty());
        uint64_t useSeq=m_senderCreditSeqList.front();
        m_senderCreditSeqList.pop();
        packet->AddPacketTag(CreditSeqTag(useSeq));
        
        NS_ASSERT(!m_senderPathTagList.empty());
        PathTag pathTag=m_senderPathTagList.front();
        NS_ASSERT(!pathTag.forward);
        m_senderPathTagList.pop();
        packet->AddPacketTag(PathTag(pathTag));

        SocketIpTosTag ipTosTag;
        ipTosTag.SetTos(m_dataPrio);
        packet->ReplacePacketTag(ipTosTag);
        RoCEv2CreditCc::UpdateStateSend(packet);
    }

    void RoCEv2CreditSpray::UpdateStateWithRcvACK(Ptr<Packet> ack,
                               const RoCEv2Header& roce,
                               const uint32_t senderNextPSN)
    {
        NS_LOG_FUNCTION(this << ack << roce << senderNextPSN);

        CreditSeqTag csTag;
        PathTag pathTag;
        bool hasPathTag = ack->PeekPacketTag(pathTag);
        NS_ASSERT(hasPathTag);
        NS_ASSERT(pathTag.forward);
        bool hasCsTag = ack->PeekPacketTag(csTag);
        NS_ASSERT(hasCsTag);

        pathTag.forward=false;
        m_senderPathTagList.push(PathTag(pathTag));
        m_senderCreditSeqList.push(csTag.GetSeq());

        int32_t ackedPkts =
        std::max((int32_t)0, (int32_t)roce.GetPSN() - (int32_t)m_sockState->GetTxBuffer()->GetFrontPsn());
    /**
     * When receiving a credit, we want the right bound of the window +1, strictly.
     * To achieve this, we do these operations:
     * 1. cwnd -= ackedPkts: the ackedPkts is how many packets the left bound moved. We minus it to
     * make the right bound do not move.
     * 2. cwnd += 1: make the right bound move 1 packet.
     */
    int32_t cwndToSet = m_sockState->GetCwnd() + ((int32_t)1 - ackedPkts) * (int32_t)m_sockState->GetPacketSize();
    NS_ASSERT_MSG(cwndToSet >= 0, "CWND to set is negative!");
    m_sockState->SetCwnd(cwndToSet);
    //m_sockState->SetCwnd(m_sockState->GetCwnd() + (1 - ackedPkts) * m_sockState->GetPacketSize());
    m_sendPendingDataCb(); // Trigger sending pending data packets

    // Stop sending further credit requests once any ACK is received
    if (m_cReqTimeOut.IsRunning())
    {
        m_cReqTimeOut.Cancel();
    }

    //If all data are acknowledged, send a stop-credit message once
    if (roce.GetPSN() == m_sockState->GetTxBuffer()->GetEndPsn() &&
        m_recvAckAfterFinish++ % 2000 == 0)
    {
        
        CongestionTypeTag ctTag(GetTypeId().GetUid());
        CreditRequestTag crTag(false);
        SocketIpTosTag ipTosTag;
        ipTosTag.SetTos(m_dataPrio);
        std::vector<std::reference_wrapper<const Tag>> packetTags{ctTag, crTag,ipTosTag};
        
        bool success =
            m_sendOutbandPktCb(m_sockState->GetTxBuffer()->GetEndPsn(), true, packetTags);
        if (!success)
        {
            NS_LOG_WARN("Send stop Credit ACK signal failed!");
        }
        //std::cout << "flow finish" << std::endl;
    }
    }
    void
    RoCEv2CreditSpray::SendCreditRequest(Time rto)
    {
    NS_LOG_FUNCTION(this << rto);
    // To stop sending, we set the cwnd to 0
    m_sockState->SetCwnd(0);

    // Check if a Req is just sent
    if (m_cReqTimeOut.IsRunning())
    {
        return;
    }

    NS_ASSERT_MSG(!m_sendOutbandPktCb.IsNull(), "SendOutbandPktCb not set!");
    // Check if the flow is stopped
    if (CheckStopCondition())
    {
        return;
    }

    CongestionTypeTag ctTag(GetTypeId().GetUid());
    CreditRequestTag crTag(true);
    SocketIpTosTag ipTosTag;
    ipTosTag.SetTos(m_dataPrio);

    std::vector<std::reference_wrapper<const Tag>> packetTags{ctTag, crTag,ipTosTag};

    // Send out-of-band credit request packet
    bool success = m_sendOutbandPktCb(0, true, packetTags);
    if (success)
    {
        // m_probeSeq += 1;
        // // Log the time and seq of the probe
        NS_LOG_DEBUG(Simulator::Now().GetNanoSeconds() << " Send Credit Req ");
    }
    else
    {
        NS_LOG_WARN("Send Credit Req failed!");
    }
    // Start probe
    ScheduleNextCreditReq(rto);
    }

void
RoCEv2CreditSpray::SendCreditAck(uint32_t psn)
{
    NS_LOG_FUNCTION(this << psn);
    if (CheckStopCondition())
    {
        return;
    }

    CreditSeqTag csTag(m_nextCreditSeq++);
    CongestionTypeTag ctTag(GetTypeId().GetUid());
    SocketIpTosTag ipTosTag;
    ipTosTag.SetTos(m_creditPrio);
    std::vector<std::reference_wrapper<const Tag>> packetTags{ctTag,csTag,ipTosTag};


    // Send out-of-band credit ACK packet
    bool success = m_sendOutbandPktCb(psn, false, packetTags);
    if (!success)
    {
        NS_LOG_WARN("Send Credit ACK failed!");
        return;
    }

    if (m_creditAckInterval.IsStrictlyPositive())
    {
        m_creditAckEvent = Simulator::Schedule(m_creditAckInterval,
                                               &RoCEv2CreditCc::SendCreditAck,
                                               this,
                                               psn);
    }
}

void
RoCEv2CreditSpray::RateControl(double lossRatio)
{
    //NS_LOG_FUNCTION(this);
    // uint64_t sendCredit=first_nextRTTseq-m_lastUpadateRateSeq;
    // m_lastUpadateRateSeq=first_nextRTTseq;
    // double lossRatio = static_cast<double>(m_creditLossCount) / sendCredit;
    // m_creditLossCount=0;
    NS_ASSERT(lossRatio<1&&lossRatio>=0);

    if(lossRatio<=m_targetLossRatio){

        if(m_rateControlLastAction==RATE_CONTROL_LAST_ACTION_INCREASE){
            m_aggressiveRatio=(m_aggressiveRatio+m_maxAggressiveRatio)/2;
        }

        double curRate=m_creditRate.GetBitRate();
        double maxRate=m_maxCreditRate.GetBitRate();
        double newRate=(1-m_aggressiveRatio)*curRate+\
                m_aggressiveRatio*maxRate*(1+m_targetLossRatio);
        m_creditRate=DataRate(uint64_t(newRate));

        //std::cout<<"host "<<Simulator::GetContext()<<" rate increase";
        UpdateCreditRate(m_creditRate);

        m_rateControlLastAction=RATE_CONTROL_LAST_ACTION_INCREASE;
    }
    else{
        double curRate=m_creditRate.GetBitRate();
        double newRate=(1-lossRatio)*(1+m_targetLossRatio)*curRate;
        m_creditRate=DataRate(uint64_t(newRate));

        //std::cout<<"host "<<Simulator::GetContext()<<" rate decrease";
        UpdateCreditRate(m_creditRate);

        m_aggressiveRatio=std::max(m_minAggressiveRatio,m_aggressiveRatio/2);
        m_rateControlLastAction=RATE_CONTROL_LAST_ACTION_DECREASE;
    }

}

void 
RoCEv2CreditSpray::UpdateStateWithOutbandPkt(Ptr<Packet> packet,
                                     const RoCEv2Header& roce,
                                     const uint32_t senderNextPSN)
{
    NS_LOG_FUNCTION(this << packet << roce << senderNextPSN);
    CreditRequestTag crTag;
    PathTag pathTag;
    bool hasPathTag = packet->PeekPacketTag(pathTag);
    bool hasCrTag = packet->PeekPacketTag(crTag);
    NS_ASSERT(hasPathTag);
    NS_ASSERT(hasCrTag);


    RoCEv2CreditCc::UpdateStateWithOutbandPkt(packet, roce, senderNextPSN);
}

void 
RoCEv2CreditSpray::StartCreditAckLoop(const RoCEv2Header& roce)
{
    double lineRate = static_cast<double>(m_sockState->GetDeviceRate()->GetBitRate()); // bits/s
    double dataBytes = static_cast<double>(m_sockState->GetPacketSize());
    double sendRateBits = lineRate * (m_ackPacketSize / (dataBytes+m_ackPacketSize)); // bits/s
    m_maxCreditRate=DataRate(uint64_t(sendRateBits));
    m_creditRate=DataRate(uint64_t(sendRateBits*m_initCreditRateRatio));

    //m_rateControlInterval=m_sockState->GetBaseRtt(); baseRTT=0???
    NS_LOG_FUNCTION(this << roce);
    UpdateCreditRate(m_creditRate);

    //RoCEv2CreditCc::StartCreditAckLoop(roce);
    if(m_creditAckEvent.IsRunning()){
        m_creditAckEvent.Cancel();
    }
    SendCreditAck(roce.GetPSN());
}

void
RoCEv2CreditSpray::UpdateStateRecvData(Ptr<Packet> packet)
{
    NS_LOG_FUNCTION(this << packet);

    CreditSeqTag csTag;
    bool hasTag = packet->PeekPacketTag(csTag);
    NS_ASSERT(hasTag);
    uint64_t pktSeq=csTag.GetSeq();
    //NS_ASSERT(pktSeq>m_lastRecvCreditSeq);
    //m_creditLossCount+=pktSeq-m_lastRecvCreditSeq-1;
    //m_lastRecvCreditSeq=pktSeq;
    if(pktSeq>m_lastUpadateRateSeq){
        m_recvDataCount++;
    }
    if(pktSeq>=m_nextUpdateSeq){
        if(m_nextUpdateSeq!=0){
        uint64_t sendCredit=pktSeq-m_lastUpadateRateSeq;
        m_lastUpadateRateSeq=pktSeq;
        double lossRatio = static_cast<double>(sendCredit-m_recvDataCount) / sendCredit;
        //m_creditLossCount=0;
        //NS_ASSERT(lossRatio<1&&lossRatio>=0);
        m_recvDataCount=0;
        RateControl(lossRatio);
        }
        m_nextUpdateSeq=m_nextCreditSeq;
    }

}



// CreditSeqTag implementation
TypeId
RoCEv2CreditSpray::CreditSeqTag::GetTypeId()
{
    static TypeId tid = TypeId("ns3::RoCEv2CreditSpray::CreditSeqTag")
                            .SetParent<Tag>()
                            .SetGroupName("DCB")
                            .AddConstructor<CreditSeqTag>();
    return tid;
}

RoCEv2CreditSpray::CreditSeqTag::CreditSeqTag()
    : m_seq(0)
{
}

RoCEv2CreditSpray::CreditSeqTag::CreditSeqTag(uint64_t seq)
    : m_seq(seq)
{
}

TypeId
RoCEv2CreditSpray::CreditSeqTag::GetInstanceTypeId() const
{
    return GetTypeId();
}

uint64_t
RoCEv2CreditSpray::CreditSeqTag::GetSeq() const
{
    return m_seq;
}

void
RoCEv2CreditSpray::CreditSeqTag::SetSeq(uint64_t seq)
{
    m_seq = seq;
}

uint32_t
RoCEv2CreditSpray::CreditSeqTag::GetSerializedSize() const
{
    return sizeof(m_seq);
}

void
RoCEv2CreditSpray::CreditSeqTag::Serialize(TagBuffer i) const
{
    i.WriteU64(m_seq);
}

void
RoCEv2CreditSpray::CreditSeqTag::Deserialize(TagBuffer i)
{
    m_seq = i.ReadU64();
}

void
RoCEv2CreditSpray::CreditSeqTag::Print(std::ostream& os) const
{
    os << "CreditSeq=" << m_seq;
}
}