/*
 * Copyright (c) 2024
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * NDP Traffic Generation Application
 * Specialized application for NDP protocol testing
 */

 #ifndef NDP_TRAFFIC_GEN_APPLICATION_H
 #define NDP_TRAFFIC_GEN_APPLICATION_H
 
 #include "ndp-socket.h"
 
 #include "ns3/application.h"
 #include "ns3/data-rate.h"
 #include "ns3/dc-topology.h"
 #include "ns3/event-id.h"
 #include "ns3/ipv4-address.h"
 #include "ns3/random-variable-stream.h"
 #include "ns3/traced-callback.h"
 
 #include <vector>
 #include <utility>
 
 namespace ns3
 {
 
 /**
  * \ingroup dcb
  * \brief Traffic generation application for NDP
  *
  * This application generates various traffic patterns for testing NDP:
  * - SEND_ONCE: Send a fixed amount of data once
  * - CONTINUOUS: Continuously send data at a specified rate
  * - INCAST: Multiple senders to one receiver
  * - BURST: Periodic bursts of traffic
  */
 class NdpTrafficGenApplication : public Application
 {
   public:
     /**
      * \brief Traffic patterns
      */
     /**
      * \brief CDF trace type: vector of (flowSizeBytes, probability) pairs
      */
     typedef std::vector<std::pair<uint32_t, double>> TraceCdf;

     enum TrafficPattern
     {
         SEND_ONCE,   ///< Send once and stop
         CONTINUOUS,  ///< Continuous sending
         INCAST,      ///< Incast pattern (multiple senders to one receiver)
         BURST,       ///< Periodic bursts
         CDF          ///< CDF-based traffic generation (multiple flows per host)
     };
 
     /**
      * \brief Get the type ID.
      * \return the object TypeId
      */
     static TypeId GetTypeId();
 
     NdpTrafficGenApplication();
     virtual ~NdpTrafficGenApplication();
 
     /**
      * \brief Set the NDP socket
      * \param socket NDP socket to use
      */
     void SetSocket(Ptr<NdpSocket> socket);
 
     /**
      * \brief Set multipath IPs
      * \param paths Vector of destination IPs (one per path)
      *
      * Must be called before Connect()
      */
     void SetPaths(const std::vector<Ipv4Address>& paths);
 
     /**
      * \brief Set remote address and port
      * \param ip Remote IP address (can be any path IP)
      * \param port Remote port
      */
     void SetRemote(Ipv4Address ip, uint16_t port);
 
     /**
      * \brief Set traffic pattern
      * \param pattern Traffic pattern to use
      */
     void SetTrafficPattern(TrafficPattern pattern);
 
     /**
      * \brief Set total flow size
      * \param bytes Total bytes to send
      */
     void SetFlowSize(uint64_t bytes);
 
     /**
      * \brief Set packet size
      * \param size Packet size in bytes
      */
     void SetPacketSize(uint32_t size);
 
     /**
      * \brief Set data rate (for continuous/burst patterns)
      * \param rate Data rate
      */
     void SetDataRate(DataRate rate);
 
     /**
      * \brief Set burst parameters
      * \param burstSize Bytes per burst
      * \param burstInterval Time between bursts
      */
     void SetBurstParams(uint32_t burstSize, Time burstInterval);

     // ────────── CDF traffic pattern support ──────────

     /**
      * \brief Set the CDF for flow size distribution
      * \param cdf CDF trace (flowSize, probability) pairs
      */
     void SetFlowCdf(const TraceCdf& cdf);

     /**
      * \brief Set traffic load for CDF pattern
      * \param load Traffic load (0..1)
      */
     void SetTrafficLoad(double load);

     /**
      * \brief Set host link rate for CDF arrival calculation
      * \param rate Host link data rate
      */
     void SetLinkRate(DataRate rate);

     /**
      * \brief Set topology info for random destination selection
      * \param topology DC topology
      * \param nodeIndex This node's index in topology
      */
     void SetTopologyInfo(Ptr<DcTopology> topology, uint32_t nodeIndex);

     /**
      * \brief Set the CDF generation stop time
      * \param stopTime Time at which to stop generating new flows
      */
     void SetCdfStopTime(Time stopTime);

     /**
      * \brief Get the number of CDF flows generated
      * \return Number of CDF flows
      */
     uint32_t GetCdfFlowCount() const;

     /**
      * \brief Get all CDF flow sockets (for stats collection)
      */
     const std::vector<Ptr<NdpSocket>>& GetCdfSockets() const;
 
     /**
      * \brief Get flow completion time
      * \return FCT in seconds
      */
     Time GetFlowCompletionTime() const;
 
     /**
      * \brief Get total bytes sent
      * \return Bytes sent
      */
     uint64_t GetBytesSent() const;
 
    /**
     * \brief Get total packets sent
     * \return Packets sent
     */
    uint64_t GetPacketsSent() const;

    /**
     * \brief Get flow start time
     * \return Start time
     */
    Time GetStartTime() const;

    /**
     * \brief Get flow finish time
     * \return Finish time
     */
    Time GetFinishTime() const;

    /**
     * \brief Check if sending is enabled
     * \return True if this is a sender application
     */
    bool IsSendEnabled() const;

    /**
     * \brief Get the NDP socket
     * \return NDP socket pointer
     */
    Ptr<NdpSocket> GetSocket() const;

    /**
     * \brief Get per-flow stats (convenience wrapper around socket->GetFlowStats())
     * \return const reference to FlowStats, or default if socket is null
     */
    const NdpSocket::FlowStats& GetNdpFlowStats() const;

  protected:
     virtual void StartApplication() override;
     virtual void StopApplication() override;
 
   private:
     /**
      * \brief Send a single packet
      */
     void SendPacket();
 
     /**
      * \brief Schedule next transmission
      */
     void ScheduleTx();
 
     /**
      * \brief Handle data reception
      * \param socket Socket that received data (called on each accepted socket)
      */
     void HandleRead(Ptr<Socket> socket);

     /**
      * \brief Handle new accepted connection (receiver side)
      *
      * Called by the LISTEN socket's "new connection created" callback when
      * NdpL4Protocol creates a per-flow accepted socket.  This method
      * registers HandleRead on the accepted socket so data can be read.
      *
      * \param socket Newly accepted per-flow socket
      * \param from   Remote address of the new flow
      */
     void HandleAccept(Ptr<Socket> socket, const Address& from);

     /**
      * \brief Handle flow completion (all packets ACKed by receiver)
      *
      * Called via NdpSocket::m_flowCompleteCallback when ProcessAck() detects
      * that txBuffer is empty and all sequence numbers have been sent and
      * acknowledged.  This is the true end-to-end flow completion event.
      */
     void HandleFlowComplete();

     /**
      * \brief Handle connection succeeded
      * \param socket Socket that connected
      */
     void ConnectionSucceeded(Ptr<Socket> socket);
 
     /**
      * \brief Handle connection failed
      * \param socket Socket that failed
      */
     void ConnectionFailed(Ptr<Socket> socket);

     // ────────── CDF private helpers ──────────

     /**
      * \brief Generate CDF traffic (pre-schedule all flows)
      */
     void GenerateCdfTraffic();

     /**
      * \brief Schedule one CDF flow at the given time
      * \param startTime Simulation time to start the flow
      */
     void ScheduleNextCdfFlow(const Time& startTime);

     /**
      * \brief Send all data for a CDF flow
      * \param socket The NdpSocket for this flow
      * \param flowSize Bytes to send
      */
     void SendCdfFlow(Ptr<NdpSocket> socket, uint64_t flowSize);

     /**
      * \brief Get next flow size from CDF distribution
      */
     uint32_t GetNextCdfFlowSize() const;

     /**
      * \brief Get next inter-arrival interval (exponential)
      */
     Time GetNextFlowArriveInterval() const;

     /**
      * \brief Get random destination host index (≠ self)
      */
     uint32_t GetRandomDestNode();
 
     // Configuration
     Ptr<NdpSocket> m_socket;          ///< NDP socket (listen/single-flow)
     std::vector<Ipv4Address> m_paths; ///< Multipath destination IPs
     Ipv4Address m_remoteIp;           ///< Remote IP address
     uint16_t m_remotePort;            ///< Remote port
     TrafficPattern m_pattern;         ///< Traffic pattern
     uint64_t m_flowSize;              ///< Total bytes to send
     uint32_t m_packetSize;            ///< Packet size
     DataRate m_dataRate;              ///< Sending rate
     bool m_sendEnabled;               ///< Whether this app should send data
 
     // Burst parameters
     uint32_t m_burstSize;       ///< Bytes per burst
     Time m_burstInterval;       ///< Time between bursts
     uint32_t m_currentBurstSent; ///< Bytes sent in current burst
 
     // State
     uint64_t m_bytesSent;    ///< Total bytes sent
     uint64_t m_packetsSent;  ///< Total packets sent
     EventId m_sendEvent;     ///< Send event
     Time m_startTime;        ///< Flow start time
     Time m_finishTime;       ///< Flow finish time
     bool m_connected;        ///< Connection status
     bool m_finished;         ///< Flow finished flag

     // ────────── CDF traffic pattern members ──────────
     Ptr<EmpiricalRandomVariable> m_flowSizeRng;       ///< Flow size CDF random variable
     Ptr<ExponentialRandomVariable> m_flowArriveTimeRng; ///< Flow inter-arrival RNG
     Ptr<UniformRandomVariable> m_hostIndexRng;         ///< Random destination chooser
     double m_trafficLoad;                              ///< Target traffic load (0..1)
     DataRate m_linkRate;                               ///< Host link rate
     uint64_t m_avgFlowSize;                            ///< Mean flow size from CDF (bytes)
     Ptr<DcTopology> m_topology;                        ///< Topology for dest selection
     uint32_t m_nodeIndex;                              ///< This host's index in topology
     Time m_cdfStopTime;                                ///< Stop generating CDF flows
     uint32_t m_cdfFlowCount;                           ///< Count of CDF flows generated
     std::vector<Ptr<NdpSocket>> m_cdfSockets;          ///< All CDF flow sockets (for stats)
     Ptr<NdpSocket> m_listenSocket;                     ///< Separate listen socket for CDF mode
 
     // Traced callbacks
     TracedCallback<Ptr<const Packet>> m_txTrace; ///< Transmit trace
 };
 
 } // namespace ns3
 
 #endif /* NDP_TRAFFIC_GEN_APPLICATION_H */
 