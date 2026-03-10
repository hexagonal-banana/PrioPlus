/*
 * Copyright (c) 2024
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * NDP Traffic Generation Application Implementation
 */

 #include "ndp-traffic-gen-application.h"

 #include "ns3/boolean.h"
 #include "ns3/double.h"
 #include "ns3/inet-socket-address.h"
 #include "ns3/ipv4.h"
 #include "ns3/log.h"
 #include "ns3/ndp-l4-protocol.h"
 #include "ns3/ndp-multipath-helper.h"
 #include "ns3/packet.h"
 #include "ns3/simulator.h"
 #include "ns3/uinteger.h"
 #include <fstream>
 #include <chrono>
 
 namespace ns3
 {
 
 NS_LOG_COMPONENT_DEFINE("NdpTrafficGenApplication");
 
 NS_OBJECT_ENSURE_REGISTERED(NdpTrafficGenApplication);
 
 TypeId
 NdpTrafficGenApplication::GetTypeId()
 {
     static TypeId tid =
         TypeId("ns3::NdpTrafficGenApplication")
             .SetParent<Application>()
             .SetGroupName("Dcb")
             .AddConstructor<NdpTrafficGenApplication>()
             .AddAttribute("PacketSize",
                           "Size of packets to send",
                           UintegerValue(1400),
                           MakeUintegerAccessor(&NdpTrafficGenApplication::m_packetSize),
                           MakeUintegerChecker<uint32_t>())
             .AddAttribute("FlowSize",
                           "Total size of flow in bytes",
                           UintegerValue(1000000),
                           MakeUintegerAccessor(&NdpTrafficGenApplication::m_flowSize),
                           MakeUintegerChecker<uint64_t>())
             .AddAttribute("SendEnabled",
                           "Whether this application should send data",
                           BooleanValue(true),
                           MakeBooleanAccessor(&NdpTrafficGenApplication::m_sendEnabled),
                           MakeBooleanChecker())
             .AddTraceSource("Tx",
                             "A packet is sent",
                             MakeTraceSourceAccessor(&NdpTrafficGenApplication::m_txTrace),
                             "ns3::Packet::TracedCallback");
     return tid;
 }
 
 NdpTrafficGenApplication::NdpTrafficGenApplication()
     : m_socket(nullptr),
       m_remoteIp("0.0.0.0"),
       m_remotePort(0),
       m_pattern(SEND_ONCE),
       m_flowSize(1000000),
       m_packetSize(1400),
       m_dataRate(DataRate("10Gbps")),
       m_sendEnabled(true),
       m_burstSize(10000),
       m_burstInterval(MilliSeconds(10)),
       m_currentBurstSent(0),
       m_bytesSent(0),
       m_packetsSent(0),
       m_connected(false),
       m_finished(false),
       m_flowSizeRng(nullptr),
       m_flowArriveTimeRng(nullptr),
       m_hostIndexRng(nullptr),
       m_trafficLoad(0.5),
       m_linkRate(DataRate("100Gbps")),
       m_avgFlowSize(0),
       m_topology(nullptr),
       m_nodeIndex(0),
       m_cdfStopTime(Seconds(0)),
       m_cdfFlowCount(0),
       m_listenSocket(nullptr)
 {
     NS_LOG_FUNCTION(this);
 }
 
 NdpTrafficGenApplication::~NdpTrafficGenApplication()
 {
     NS_LOG_FUNCTION(this);
 }
 
 void
 NdpTrafficGenApplication::SetSocket(Ptr<NdpSocket> socket)
 {
     NS_LOG_FUNCTION(this << socket);
     m_socket = socket;
 }
 
 void
 NdpTrafficGenApplication::SetPaths(const std::vector<Ipv4Address>& paths)
 {
     NS_LOG_FUNCTION(this << paths.size());
     m_paths = paths;
 }
 
 void
 NdpTrafficGenApplication::SetRemote(Ipv4Address ip, uint16_t port)
 {
     NS_LOG_FUNCTION(this << ip << port);
     m_remoteIp = ip;
     m_remotePort = port;
 }
 
 void
 NdpTrafficGenApplication::SetTrafficPattern(TrafficPattern pattern)
 {
     NS_LOG_FUNCTION(this << pattern);
     m_pattern = pattern;
 }
 
 void
 NdpTrafficGenApplication::SetFlowSize(uint64_t bytes)
 {
     NS_LOG_FUNCTION(this << bytes);
     m_flowSize = bytes;
 }
 
 void
 NdpTrafficGenApplication::SetPacketSize(uint32_t size)
 {
     NS_LOG_FUNCTION(this << size);
     m_packetSize = size;
 }
 
 void
 NdpTrafficGenApplication::SetDataRate(DataRate rate)
 {
     NS_LOG_FUNCTION(this << rate);
     m_dataRate = rate;
 }
 
 void
 NdpTrafficGenApplication::SetBurstParams(uint32_t burstSize, Time burstInterval)
 {
     NS_LOG_FUNCTION(this << burstSize << burstInterval);
     m_burstSize = burstSize;
     m_burstInterval = burstInterval;
 }
 
 // ────────── CDF pattern setters ──────────

 void
 NdpTrafficGenApplication::SetFlowCdf(const TraceCdf& cdf)
 {
     NS_LOG_FUNCTION(this);

     m_flowSizeRng = CreateObject<EmpiricalRandomVariable>();
     m_flowSizeRng->SetAttribute("Interpolate", BooleanValue(true));

     // Feed the CDF and compute the mean flow size
     double meanSize = 0.0;
     auto [ls, lp] = cdf[0];
     for (const auto& [sz, prob] : cdf)
     {
         m_flowSizeRng->CDF(sz, prob);
         meanSize += (sz + ls) / 2.0 * (prob - lp);
         ls = sz;
         lp = prob;
     }
     m_avgFlowSize = static_cast<uint64_t>(meanSize);
     NS_LOG_INFO("CDF loaded: mean flow size = " << m_avgFlowSize << " bytes");
 }

 void
 NdpTrafficGenApplication::SetTrafficLoad(double load)
 {
     m_trafficLoad = load;
 }

 void
 NdpTrafficGenApplication::SetLinkRate(DataRate rate)
 {
     m_linkRate = rate;
 }

 void
 NdpTrafficGenApplication::SetTopologyInfo(Ptr<DcTopology> topology, uint32_t nodeIndex)
 {
     m_topology = topology;
     m_nodeIndex = nodeIndex;
     // Create random host chooser: uniform [0, nHosts-1]
     m_hostIndexRng = CreateObject<UniformRandomVariable>();
     m_hostIndexRng->SetAttribute("Min", DoubleValue(0));
     m_hostIndexRng->SetAttribute("Max", DoubleValue(topology->GetNHosts() - 1));
 }

 void
 NdpTrafficGenApplication::SetCdfStopTime(Time stopTime)
 {
     m_cdfStopTime = stopTime;
 }

 uint32_t
 NdpTrafficGenApplication::GetCdfFlowCount() const
 {
     return m_cdfFlowCount;
 }

 const std::vector<Ptr<NdpSocket>>&
 NdpTrafficGenApplication::GetCdfSockets() const
 {
     return m_cdfSockets;
 }

 Time
 NdpTrafficGenApplication::GetFlowCompletionTime() const
 {
     if (m_finished && m_startTime.IsStrictlyPositive())
     {
         return m_finishTime - m_startTime;
     }
     return Seconds(0);
 }
 
 uint64_t
 NdpTrafficGenApplication::GetBytesSent() const
 {
     return m_bytesSent;
 }
 
uint64_t
NdpTrafficGenApplication::GetPacketsSent() const
{
    return m_packetsSent;
}

Time
NdpTrafficGenApplication::GetStartTime() const
{
    return m_startTime;
}

Time
NdpTrafficGenApplication::GetFinishTime() const
{
    return m_finishTime;
}

bool
NdpTrafficGenApplication::IsSendEnabled() const
{
    return m_sendEnabled;
}

Ptr<NdpSocket>
NdpTrafficGenApplication::GetSocket() const
{
    return m_socket;
}

const NdpSocket::FlowStats&
NdpTrafficGenApplication::GetNdpFlowStats() const
{
    static NdpSocket::FlowStats s_empty;
    if (m_socket != nullptr)
    {
        return m_socket->GetFlowStats();
    }
    return s_empty;
}

void
NdpTrafficGenApplication::StartApplication()
{
    NS_LOG_FUNCTION(this);

    if (!m_socket)
    {
        NS_FATAL_ERROR("NdpSocket not set");
    }

    // ━━━━━━━━━━━━━ CDF mode: each host is both sender AND receiver ━━━━━━━━━━━━━
    if (m_pattern == CDF)
    {
        // 1) Set up a LISTEN socket so this host can accept incoming NDP flows
        m_listenSocket = m_socket;  // reuse the pre-created socket as listener
        InetSocketAddress localAddr(Ipv4Address::GetAny(), 4000);
        m_listenSocket->Bind(localAddr);
        m_listenSocket->SetAcceptCallback(
            MakeNullCallback<bool, Ptr<Socket>, const Address&>(),
            MakeCallback(&NdpTrafficGenApplication::HandleAccept, this));
        m_listenSocket->Listen();
        m_connected = true;
        NS_LOG_INFO("CDF mode: listen socket ready on port 4000, node " << GetNode()->GetId());

        // 2) Calculate inter-arrival rate and schedule outgoing flows
        GenerateCdfTraffic();
        return;
    }

    // ━━━━━━━━━━━━━ Non-CDF modes (SEND_ONCE / CONTINUOUS / INCAST / BURST) ━━━━━━━━━━━━━
    // Receivers must bind to a specific port (4000) so senders can find them!
    if (!m_sendEnabled || m_remoteIp == Ipv4Address("0.0.0.0"))
    {
        InetSocketAddress localAddr(Ipv4Address::GetAny(), 4000);
        m_socket->Bind(localAddr);
    }
    else
    {
        m_socket->Bind();
    }

    // Set multipath BEFORE connecting (critical for NDP)
    if (!m_paths.empty())
    {
        m_socket->SetPaths(m_paths);
        NS_LOG_DEBUG("Set " << m_paths.size() << " paths for socket");
    }

    // Set callbacks on the LISTEN/sender socket
    m_socket->SetConnectCallback(
        MakeCallback(&NdpTrafficGenApplication::ConnectionSucceeded, this),
        MakeCallback(&NdpTrafficGenApplication::ConnectionFailed, this));

    // Receiver vs Sender setup
    if (!m_sendEnabled || m_remoteIp == Ipv4Address("0.0.0.0"))
    {
        m_socket->SetAcceptCallback(
            MakeNullCallback<bool, Ptr<Socket>, const Address&>(),
            MakeCallback(&NdpTrafficGenApplication::HandleAccept, this));

        m_socket->Listen();
        m_connected = true;
        NS_LOG_INFO("Socket listening for incoming NDP connections");
    }
    else
    {
        InetSocketAddress remote = InetSocketAddress(m_remoteIp, m_remotePort);
        int ret = m_socket->Connect(remote);

        if (ret < 0)
        {
            NS_LOG_ERROR("Connection failed immediately");
        }
        else
        {
            NS_LOG_INFO("Connection initiated to " << m_remoteIp << ":" << m_remotePort);
        }
    }
}
 
 void
 NdpTrafficGenApplication::StopApplication()
 {
     NS_LOG_FUNCTION(this);
 
     if (m_sendEvent.IsRunning())
     {
         Simulator::Cancel(m_sendEvent);
     }
 
     if (m_socket)
     {
         m_socket->Close();
     }
 
     if (!m_finished && m_bytesSent > 0)
     {
         m_finishTime = Simulator::Now();
         m_finished = true;
 
         Time fct = GetFlowCompletionTime();
         NS_LOG_INFO("Flow stopped: sent " << m_bytesSent << " bytes in " << fct.GetMicroSeconds()
                                           << " us (FCT)");
     }
 }
 
void
NdpTrafficGenApplication::ConnectionSucceeded(Ptr<Socket> socket)
{
     NS_LOG_FUNCTION(this << socket);
     
     m_connected = true;
     m_startTime = Simulator::Now();

     NS_LOG_INFO("Connection succeeded at " << m_startTime.GetSeconds() << "s");

     // Register the flow-complete callback so that m_finishTime is set only
     // when the NdpSocket has received ACKs for ALL outstanding packets.
     if (m_sendEnabled && m_socket)
     {
         m_socket->SetFlowCompleteCallback(
             MakeCallback(&NdpTrafficGenApplication::HandleFlowComplete, this));
     }
 
     // Only start sending if enabled
     if (!m_sendEnabled)
     {
         // std::cout << "DEBUG: Node " << GetNode()->GetId() << " is receiver-only (SendEnabled=false)" << std::endl;
         return;
     }
 
     // Start sending based on pattern
     switch (m_pattern)
     {
     case SEND_ONCE:
     case CONTINUOUS:
         SendPacket();
         break;
 
     case INCAST:
     {
         // In incast, all senders start together
         // Add small random jitter to avoid perfect synchronization
         Ptr<UniformRandomVariable> jitter = CreateObject<UniformRandomVariable>();
         jitter->SetAttribute("Min", DoubleValue(0.0));
         jitter->SetAttribute("Max", DoubleValue(0.001)); // 1ms jitter
         Simulator::Schedule(Seconds(jitter->GetValue()),
                             &NdpTrafficGenApplication::SendPacket,
                             this);
         break;
     }
 
    case BURST:
        m_currentBurstSent = 0;
        SendPacket();
        break;

    case CDF:
        // CDF traffic generation is handled by GenerateCdfTraffic(),
        // which is triggered in StartApplication, not ConnectionSucceeded.
        // This socket is only a receiver-side listener.
        GenerateCdfTraffic();
        break;
    }
}
 
 void
 NdpTrafficGenApplication::ConnectionFailed(Ptr<Socket> socket)
 {
     NS_LOG_FUNCTION(this << socket);
     NS_LOG_ERROR("Connection failed");
 }
 
void
NdpTrafficGenApplication::SendPacket()
{
    NS_LOG_FUNCTION(this);

    if (!m_connected || m_bytesSent >= m_flowSize)
    {
        // All data already submitted; true completion is via HandleFlowComplete()
        return;
    }

    // For SEND_ONCE and INCAST patterns, submit the entire flow at once
    // (similar to RoCEv2's approach)
    if ((m_pattern == SEND_ONCE || m_pattern == INCAST) && m_bytesSent == 0)
    {
        NS_LOG_INFO("Submitting entire flow at once (" << m_flowSize << " bytes)");
        
        // Loop to send all packets of the flow
        while (m_bytesSent < m_flowSize)
        {
            uint32_t toSend = std::min((uint64_t)m_packetSize, m_flowSize - m_bytesSent);
            Ptr<Packet> packet = Create<Packet>(toSend);
            
            int ret = m_socket->Send(packet, 0);
            
            if (ret > 0)
            {
                m_bytesSent += toSend;
                m_packetsSent++;
                m_txTrace(packet);
                
                NS_LOG_DEBUG("Submitted packet " << m_packetsSent << ": " << toSend 
                            << " bytes (total: " << m_bytesSent << "/" << m_flowSize << ")");
            }
            else if (ret == 0)
            {
                // Socket buffer full, schedule retry
                NS_LOG_DEBUG("Socket buffer full at packet " << (m_packetsSent + 1) 
                            << ", retrying in 1us");
                Simulator::Schedule(MicroSeconds(1), 
                                   &NdpTrafficGenApplication::SendPacket, 
                                   this);
                return;
            }
            else
            {
                // Error occurred
                NS_LOG_ERROR("Send failed with error code " << ret << " at node " 
                            << GetNode()->GetId());
                return;
            }
        }
        
        // All packets submitted to socket buffer.
        // NOTE: Do NOT set m_finished/m_finishTime here.
        // True flow completion (all-ACKed) is signalled via HandleFlowComplete().
        NS_LOG_INFO("All " << m_bytesSent << " bytes submitted to socket at "
                   << Simulator::Now().GetMicroSeconds() << " us, awaiting ACKs...");
        return;
    }

    // For BURST pattern, submit the entire burst at once
    if (m_pattern == BURST && m_currentBurstSent == 0)
    {
        uint64_t burstEnd = std::min(m_bytesSent + m_burstSize, m_flowSize);
        uint64_t burstBytes = burstEnd - m_bytesSent;
        
        NS_LOG_INFO("Submitting burst at once (" << burstBytes << " bytes)");
        
        // Loop to send all packets in this burst
        while (m_bytesSent < burstEnd)
        {
            uint32_t toSend = std::min((uint64_t)m_packetSize, burstEnd - m_bytesSent);
            Ptr<Packet> packet = Create<Packet>(toSend);
            
            int ret = m_socket->Send(packet, 0);
            
            if (ret > 0)
            {
                m_bytesSent += toSend;
                m_packetsSent++;
                m_currentBurstSent += toSend;
                m_txTrace(packet);
                
                NS_LOG_DEBUG("Submitted burst packet " << m_packetsSent << ": " << toSend 
                            << " bytes (burst: " << m_currentBurstSent << "/" << m_burstSize << ")");
            }
            else if (ret == 0)
            {
                // Socket buffer full, schedule retry
                NS_LOG_DEBUG("Socket buffer full at burst packet " << (m_packetsSent + 1) 
                            << ", retrying in 1us");
                Simulator::Schedule(MicroSeconds(1), 
                                   &NdpTrafficGenApplication::SendPacket, 
                                   this);
                return;
            }
            else
            {
                // Error occurred
                NS_LOG_ERROR("Send failed with error code " << ret << " at node " 
                            << GetNode()->GetId());
                return;
            }
        }
        
        // Burst submitted successfully
        if (m_bytesSent >= m_flowSize)
        {
            // All bytes submitted; true completion is via HandleFlowComplete()
            NS_LOG_INFO("All " << m_bytesSent << " bytes (burst) submitted, awaiting ACKs...");
            return;
        }
        else
        {
            // Schedule next burst
            m_currentBurstSent = 0;
            Simulator::Schedule(m_burstInterval, 
                               &NdpTrafficGenApplication::SendPacket, 
                               this);
            return;
        }
    }

    // For other patterns (CONTINUOUS, BURST), send one packet at a time
    uint32_t toSend = std::min((uint64_t)m_packetSize, m_flowSize - m_bytesSent);
    Ptr<Packet> packet = Create<Packet>(toSend);

    int ret = m_socket->Send(packet, 0);

    if (ret > 0)
    {
        m_bytesSent += toSend;
        m_packetsSent++;
        m_txTrace(packet);

        NS_LOG_DEBUG("Sent packet " << m_packetsSent << ": " << toSend << " bytes (total: "
                                    << m_bytesSent << "/" << m_flowSize << ")");
    }
    else if (ret == 0)
    {
        // Socket buffer full, will retry later
        NS_LOG_DEBUG("Send returned 0 (buffer full), will retry");
    }
    else
    {
        // Error occurred
        NS_LOG_WARN("Send failed with error code " << ret);
    }

    // Schedule next transmission based on pattern
    if (m_bytesSent < m_flowSize)
    {
        ScheduleTx();
    }
    else
    {
        // All bytes submitted; true completion is via HandleFlowComplete()
        NS_LOG_INFO("All " << m_bytesSent << " bytes submitted, awaiting ACKs...");
    }
}
 
 void
 NdpTrafficGenApplication::ScheduleTx()
 {
     NS_LOG_FUNCTION(this);
 
     Time nextTime;
 
    switch (m_pattern)
    {
    case SEND_ONCE:
        // Send as fast as possible (let NDP control rate)
        // CRITICAL FIX: Use 0us to send back-to-back, allowing NDP FirstWindow to work correctly
        nextTime = MicroSeconds(0);
        break;
 
     case CONTINUOUS:
         // Send at specified data rate
         nextTime = Seconds(m_packetSize * 8.0 / m_dataRate.GetBitRate());
         break;
 
    case INCAST:
        // Incast: send as fast as possible (NDP will handle congestion)
        // CRITICAL FIX: Use 0us for back-to-back sending
        nextTime = MicroSeconds(0);
        break;
 
    case BURST:
        m_currentBurstSent += m_packetSize;
        if (m_currentBurstSent >= m_burstSize)
        {
            // Burst complete, wait for next burst
            m_currentBurstSent = 0;
            nextTime = m_burstInterval;
        }
        else
        {
            // Continue burst - send back-to-back
            nextTime = MicroSeconds(0);
        }
        break;
 
     default:
         nextTime = MilliSeconds(1);
         break;
     }
     m_sendEvent = Simulator::Schedule(nextTime, &NdpTrafficGenApplication::SendPacket, this);
 }
 
void
NdpTrafficGenApplication::HandleFlowComplete()
{
    NS_LOG_FUNCTION(this);

    if (!m_finished)
    {
        m_finishTime = Simulator::Now();
        m_finished = true;

        Time fct = GetFlowCompletionTime();
        NS_LOG_INFO("Flow fully complete (all-ACKed): " << m_bytesSent << " bytes, FCT="
                    << fct.GetMicroSeconds() << " us (start="
                    << m_startTime.GetMicroSeconds() << "us finish="
                    << m_finishTime.GetMicroSeconds() << "us)");
    }
}

void
NdpTrafficGenApplication::HandleAccept(Ptr<Socket> socket, const Address& from)
{
    NS_LOG_FUNCTION(this << socket << from);
    // A new per-flow accepted socket has been created by NdpL4Protocol.
    // Register the data-recv callback on it so that HandleRead() is invoked
    // whenever in-order data is delivered by this socket's Recv() path.
    socket->SetRecvCallback(MakeCallback(&NdpTrafficGenApplication::HandleRead, this));
    NS_LOG_INFO("Accepted new NDP flow from "
                << InetSocketAddress::ConvertFrom(from).GetIpv4()
                << ":" << InetSocketAddress::ConvertFrom(from).GetPort());
}

void
NdpTrafficGenApplication::HandleRead(Ptr<Socket> socket)
{
    NS_LOG_FUNCTION(this << socket);
    Ptr<Packet> packet;
    Address from;
    while ((packet = socket->RecvFrom(from)))
    {
        NS_LOG_DEBUG("Received " << packet->GetSize() << " bytes from "
                                 << InetSocketAddress::ConvertFrom(from).GetIpv4());
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  CDF traffic pattern implementation
// ════════════════════════════════════════════════════════════════════════════

void
NdpTrafficGenApplication::GenerateCdfTraffic()
{
    NS_LOG_FUNCTION(this);

    NS_ASSERT_MSG(m_flowSizeRng, "CDF not set — call SetFlowCdf() before starting CDF traffic");
    NS_ASSERT_MSG(m_topology, "Topology not set — call SetTopologyInfo() before starting CDF traffic");
    NS_ASSERT_MSG(m_avgFlowSize > 0, "Average flow size is 0 — CDF may be empty");

    // ── Compute mean inter-arrival time (same formula as DcbTrafficGenApplication) ──
    //   flowMeanInterval = avgFlowSize * 8 / (linkRate * load)    [in seconds]
    //   converted to nanoseconds for ExponentialRandomVariable
    double flowMeanIntervalNs =
        static_cast<double>(m_avgFlowSize) * 8.0
        / (m_linkRate.GetBitRate() * m_trafficLoad) * 1e9;

    m_flowArriveTimeRng = CreateObject<ExponentialRandomVariable>();
    m_flowArriveTimeRng->SetAttribute("Mean", DoubleValue(flowMeanIntervalNs));

    NS_LOG_INFO("CDF traffic: node " << m_nodeIndex
                << " avgFlowSize=" << m_avgFlowSize
                << "B load=" << m_trafficLoad
                << " linkRate=" << m_linkRate
                << " meanInterval=" << flowMeanIntervalNs << "ns"
                << " window=[" << Simulator::Now().GetSeconds()
                << "s," << m_cdfStopTime.GetSeconds() << "s)");

    // ── Pre-schedule all CDF flows ──────────────────────────────────────────
    for (Time t = Simulator::Now() + GetNextFlowArriveInterval();
         t < m_cdfStopTime;
         t += GetNextFlowArriveInterval())
    {
        // Defer ScheduleNextCdfFlow to avoid RNG pollution (same trick as RoCEv2)
        Simulator::Schedule(Time(0),
                            &NdpTrafficGenApplication::ScheduleNextCdfFlow,
                            this, t);
    }
}

void
NdpTrafficGenApplication::ScheduleNextCdfFlow(const Time& startTime)
{
    NS_LOG_FUNCTION(this << startTime);

    // 1. Pick random destination (≠ self)
    uint32_t destNode = GetRandomDestNode();

    // 2. Get destination node's primary IP
    Ptr<Node> destNodePtr = m_topology->GetNode(destNode).nodePtr;
    Ptr<Ipv4> destIpv4 = destNodePtr->GetObject<Ipv4>();
    NS_ASSERT_MSG(destIpv4 && destIpv4->GetNInterfaces() > 1,
                  "Dest node " << destNode << " has no valid IPv4");
    Ipv4Address destAddr = destIpv4->GetAddress(1, 0).GetLocal();

    // 3. Create a new NdpSocket for this flow
    Ptr<NdpL4Protocol> ndpL4 = GetNode()->GetObject<NdpL4Protocol>();
    NS_ASSERT_MSG(ndpL4, "NdpL4Protocol not installed on node " << GetNode()->GetId());

    Ptr<NdpSocket> socket = CreateObject<NdpSocket>();
    socket->SetNdp(ndpL4);

    // 4. Set multipath paths for destination
    std::vector<Ipv4Address> destPathIps =
        NdpMultipathHelper::GetInstance().GetPathIps(destNode);
    if (destPathIps.empty())
    {
        destPathIps.push_back(destAddr);
    }
    socket->SetPaths(destPathIps);

    // 5. Bind & Connect
    socket->Bind();
    socket->Connect(InetSocketAddress(destAddr, 4000));

    // 6. Draw flow size from CDF
    uint64_t flowSize = GetNextCdfFlowSize();

    // 7. Track this socket for stats collection
    m_cdfSockets.push_back(socket);
    m_cdfFlowCount++;

    NS_LOG_DEBUG("CDF flow #" << m_cdfFlowCount << ": node " << m_nodeIndex
                 << " → node " << destNode << " (" << destAddr
                 << "), size=" << flowSize << "B at t=" << startTime.GetNanoSeconds() << "ns");

    // 8. Schedule sending at the target time
    Simulator::Schedule(startTime - Simulator::Now(),
                        &NdpTrafficGenApplication::SendCdfFlow,
                        this, socket, flowSize);
}

void
NdpTrafficGenApplication::SendCdfFlow(Ptr<NdpSocket> socket, uint64_t flowSize)
{
    NS_LOG_FUNCTION(this << socket << flowSize);

    uint64_t sent = 0;
    while (sent < flowSize)
    {
        uint32_t toSend = std::min(static_cast<uint64_t>(m_packetSize), flowSize - sent);
        Ptr<Packet> packet = Create<Packet>(toSend);

        int ret = socket->Send(packet, 0);
        if (ret > 0)
        {
            sent += toSend;
        }
        else if (ret == 0)
        {
            // Socket buffer full — retry in 1µs
            Simulator::Schedule(MicroSeconds(1),
                                &NdpTrafficGenApplication::SendCdfFlow,
                                this, socket, flowSize - sent);
            return;
        }
        else
        {
            NS_LOG_ERROR("CDF flow send error " << ret << " at node " << GetNode()->GetId());
            return;
        }
    }
    NS_LOG_DEBUG("CDF flow submitted " << sent << " bytes on node " << GetNode()->GetId());
}

uint32_t
NdpTrafficGenApplication::GetNextCdfFlowSize() const
{
    NS_ASSERT_MSG(m_flowSizeRng, "CDF RNG not initialised");
    return m_flowSizeRng->GetInteger();
}

Time
NdpTrafficGenApplication::GetNextFlowArriveInterval() const
{
    NS_ASSERT_MSG(m_flowArriveTimeRng, "Flow arrive time RNG not initialised");
    return NanoSeconds(m_flowArriveTimeRng->GetInteger());
}

uint32_t
NdpTrafficGenApplication::GetRandomDestNode()
{
    NS_ASSERT_MSG(m_hostIndexRng, "Host index RNG not initialised");
    uint32_t destNode;
    do
    {
        destNode = m_hostIndexRng->GetInteger();
    } while (destNode == m_nodeIndex);
    return destNode;
}

} // namespace ns3
 