#include "rocev2-credit-cc.h"

#include "rocev2-l4-protocol.h"
#include "rocev2-socket.h"

#include "ns3/global-value.h"
#include "ns3/seq-ts-header.h"
#include "ns3/simulator.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("RoCEv2CreditCc");

NS_OBJECT_ENSURE_REGISTERED(RoCEv2CreditCc);

TypeId
RoCEv2CreditCc::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::RoCEv2CreditCc")
            .SetParent<RoCEv2CongestionOps>()
            .AddConstructor<RoCEv2CreditCc>()
            .SetGroupName("Dcb")
            .AddAttribute("CreditRateRatio",
                          "Ratio for credit ACK sending rate (0~1).",
                          DoubleValue(1.0),
                          MakeDoubleAccessor(&RoCEv2CreditCc::m_creditRateRatio),
                          MakeDoubleChecker<double>(0.0, 1.0))
            .AddAttribute("AckPacketSize",
                          "Size of each credit ACK packet.",
                          UintegerValue(68),
                          MakeUintegerAccessor(&RoCEv2CreditCc::m_ackPacketSize),
                          MakeUintegerChecker<uint32_t>(30, 1054));
    return tid;
}

RoCEv2CreditCc::RoCEv2CreditCc()
    : RoCEv2CongestionOps(std::make_shared<Stats>()),
      m_stats(std::dynamic_pointer_cast<Stats>(RoCEv2CongestionOps::m_stats))
{
    NS_LOG_FUNCTION(this);
    Init();
}

RoCEv2CreditCc::RoCEv2CreditCc(Ptr<RoCEv2SocketState> sockState)
    : RoCEv2CongestionOps(sockState, std::make_shared<Stats>()),
      m_stats(std::dynamic_pointer_cast<Stats>(RoCEv2CongestionOps::m_stats))
{
    NS_LOG_FUNCTION(this);
    Init();
}

RoCEv2CreditCc::~RoCEv2CreditCc()
{
    NS_LOG_FUNCTION(this);
}

void
RoCEv2CreditCc::SetReady()
{
    NS_LOG_FUNCTION(this);
    // send credit request and set timer for it
    this->SendCreditRequest(m_sockState->GetBaseRtt() * 10); // Magic number for now
}

void
RoCEv2CreditCc::UpdateStateSend(Ptr<Packet> packet)
{
    NS_LOG_FUNCTION(this << packet);

    // Get packet's PSN from roceheader.
    RoCEv2Header roceHeader;
    bool hasTag=packet->PeekHeader(roceHeader);
    NS_ASSERT(hasTag);
    uint32_t psn = roceHeader.GetPSN();
    if (psn == m_sockState->GetTxBuffer()->GetEndPsn()-1)
    {
        CreditRequestTag crTag(false);
        packet->AddPacketTag(crTag);
    }
}

void
RoCEv2CreditCc::UpdateStateWithOutbandPkt(Ptr<Packet> packet,
                                           const RoCEv2Header& roce,
                                           const uint32_t senderNextPSN)
{
    NS_LOG_FUNCTION(this << packet << roce << senderNextPSN);
    CreditRequestTag crTag;
    if (!packet->RemovePacketTag(crTag))
    {
        return;
    }

    if (crTag.IsRequest())
    {
        this->StartCreditAckLoop(roce);
    }
    // else
    // {
    //     // Stop credit ACK loop when receiving stop signal
    //     //std::cout << "Stop credit ACK loop" << std::endl;
    //     if (m_creditAckEvent.IsRunning())
    //     {
    //         m_creditAckEvent.Cancel();
    //     }
    // }
}

void
RoCEv2CreditCc::UpdateStateWithRcvACK(Ptr<Packet> ack,
                                       const RoCEv2Header& roce,
                                       const uint32_t senderNextPSN)
{
    // ACK works as Credit in this CC
    NS_LOG_FUNCTION(this << ack << roce << senderNextPSN);
    // int32_t ackedPkts =
    //     std::max((int32_t)0, (int32_t)roce.GetPSN() - (int32_t)m_sockState->GetTxBuffer()->GetFrontPsn());
     /**
     * When receiving a credit, we want the right bound of the window +1, strictly.
     * To achieve this, we do these operations:
     * 1. cwnd -= ackedPkts: the ackedPkts is how many packets the left bound moved. We minus it to
     * make the right bound do not move.
     * 2. cwnd += 1: make the right bound move 1 packet.
     */
    // int32_t cwndToSet = m_sockState->GetCwnd() + ((int32_t)1 - ackedPkts) * (int32_t)m_sockState->GetPacketSize();
    // NS_ASSERT_MSG(cwndToSet >= 0, "CWND to set is negative!");
    // m_sockState->SetCwnd(cwndToSet);
    // //m_sockState->SetCwnd(m_sockState->GetCwnd() + (1 - ackedPkts) * m_sockState->GetPacketSize());
    m_sockState->SetCredit(m_sockState->GetCredit() + m_sockState->GetPacketSize());
    m_sendPendingDataCb(); // Trigger sending pending data packets

    // Stop sending further credit requests once any ACK is received
    if (m_cReqTimeOut.IsRunning())
    {
        m_cReqTimeOut.Cancel();
    }

    //If all data are acknowledged, send a stop-credit message once
    // if (roce.GetPSN() == m_sockState->GetTxBuffer()->GetEndPsn() &&
    //     m_recvAckAfterFinish++ % 20 == 0)
    // {
    //     CongestionTypeTag ctTag(GetTypeId().GetUid());
    //     CreditRequestTag crTag(false);
    //     std::vector<std::reference_wrapper<const Tag>> packetTags{ctTag, crTag};
    //     bool success =
    //         m_sendOutbandPktCb(m_sockState->GetTxBuffer()->GetEndPsn(), true, packetTags);
    //     if (!success)
    //     {
    //         NS_LOG_WARN("Send stop Credit ACK signal failed!");
    //     }
    // }
}

std::string
RoCEv2CreditCc::GetName() const
{
    return "RoCEv2CreditCc";
}

void
RoCEv2CreditCc::Init()
{
    RegisterCongestionType(GetTypeId());
}

void
RoCEv2CreditCc::SendCreditRequest(Time rto)
{
    NS_LOG_FUNCTION(this << rto);
    // To stop sending, we set the cwnd to 0
    //m_sockState->SetCwnd(0);
    m_sockState->SetCredit(0);
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
    std::vector<std::reference_wrapper<const Tag>> packetTags{ctTag, crTag};

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
RoCEv2CreditCc::ScheduleNextCreditReq(Time rto)
{
    NS_LOG_FUNCTION(this << rto);
    // Cancel previous probe event
    if (m_cReqTimeOut.IsRunning())
    {
        m_cReqTimeOut.Cancel();
    }
    m_cReqTimeOut = Simulator::Schedule(rto, &RoCEv2CreditCc::SendCreditRequest, this, rto);
    NS_LOG_DEBUG(Simulator::Now().GetPicoSeconds()
                 << " " << Simulator::GetContext() << " Schedule probe after "
                 << rto.GetPicoSeconds() << "ps");
}

void
RoCEv2CreditCc::StartCreditAckLoop(const RoCEv2Header& roce)
{
    NS_LOG_FUNCTION(this << roce);
    if (m_creditAckEvent.IsRunning())
    {
        m_creditAckEvent.Cancel();
    }

    // Compute interval based on current rate/size
    // Ptr<Packet> ack = RoCEv2L4Protocol::GenerateACK(roce.GetDestQP(),
    //                                                 roce.GetSrcQP(),
    //                                                 roce.GetPSN());
    // uint32_t ackBytes = ack->GetSize();
    uint32_t ackBytes = m_ackPacketSize; // XXX Magic number for now
    m_creditAckInterval = ComputeCreditAckInterval(ackBytes);

    // Kick off immediately
    SendCreditAck(roce.GetPSN());
}

Time
RoCEv2CreditCc::ComputeCreditAckInterval(uint32_t ackBytes) const
{
    NS_LOG_FUNCTION(this << ackBytes);
    // Guard against invalid state
    if (m_creditRateRatio <= 0.0 || m_creditRateRatio > 1.0)
    {
        return Time(0);
    }

    if (m_sockState->GetDeviceRate() == nullptr || m_sockState->GetPacketSize() == 0 ||
        ackBytes == 0)
    {
        return Time(0);
    }

    double lineRate = static_cast<double>(m_sockState->GetDeviceRate()->GetBitRate()); // bits/s
    double dataBytes = static_cast<double>(m_sockState->GetPacketSize());
    double ackBytesD = static_cast<double>(ackBytes);

    double sendRateBits = m_creditRateRatio * lineRate * (ackBytesD / dataBytes); // bits/s
    if (sendRateBits <= 0.0)
    {
        return Time(0);
    }

    double intervalSeconds = (ackBytesD * 8.0) / sendRateBits;
    return Seconds(intervalSeconds);
}

void RoCEv2CreditCc::UpdateCreditRate(DataRate creditRate)
{
    NS_LOG_FUNCTION(this << creditRate);
    m_creditAckInterval=Seconds(m_ackPacketSize*8.0/double(creditRate.GetBitRate()));
    // if(m_creditAckEvent.IsRunning())
    // {
    //     m_creditAckEvent.Cancel();

    //     //Simulator.schedule() ? 
    // }
    //std::cout<<"host "<<Simulator::GetContext()<<" update credit rate to "<<creditRate.GetBitRate()<<" bps"<<std::endl;
}
void
RoCEv2CreditCc::SendCreditAck(uint32_t psn)
{
    NS_LOG_FUNCTION(this << psn);
    if (CheckStopCondition())
    {
        return;
    }

    CongestionTypeTag ctTag(GetTypeId().GetUid());
    std::vector<std::reference_wrapper<const Tag>> packetTags{ctTag};

    // Send out-of-band credit ACK packet
    //NOTE: this psn is meaningless, sendOutbandPkt use the correct psn;
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

RoCEv2CreditCc::Stats::Stats()
{
    NS_LOG_FUNCTION(this);
    BooleanValue bv;
    if (GlobalValue::GetValueByNameFailSafe("detailedSenderStats", bv))
    {
        bDetailedSenderStats = bv.Get();
    }
    else
    {
        bDetailedSenderStats = false;
    }
}

void RoCEv2CreditCc::UpdateStateRecvData(Ptr<Packet> packet,
                                         const RoCEv2Header& roce)
{
    NS_LOG_FUNCTION(this << packet);

    CreditRequestTag crTag;
    if(packet->PeekPacketTag(crTag)&&crTag.IsRequest()==false)
    {
       m_endPSN=roce.GetPSN()+1;
       m_recvEndPSN=true;
    }
    
} // namespace ns3

}