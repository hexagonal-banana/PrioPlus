/*
 * Copyright (c) 2008 INRIA
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef DCB_BASE_APPLICATION_H
#define DCB_BASE_APPLICATION_H

#include "dcb-net-device.h"
#include "ndp-socket.h"
#include "rocev2-socket.h"
#include "udp-based-socket.h"

#include "ns3/application.h"
#include "ns3/data-rate.h"
#include "ns3/dc-topology.h"
#include "ns3/flow-identifier.h"
#include "ns3/inet-socket-address.h"
#include "ns3/random-variable-stream.h"
#include "ns3/rocev2-header.h"
#include "ns3/seq-ts-size-header.h"
#include "ns3/sequence-number.h"
#include "ns3/traced-callback.h"

#include <map>
#include <set>
#include <string>

namespace ns3
{

class Socket;

/**
 * \ingroup dcb
 * \brief A application that generates traffic and sends it to a destination.
 */
class DcbBaseApplication : public Application
{
  public:
    /**
     * \brief Get the type ID.
     * \return the object TypeId
     */
    static TypeId GetTypeId(void);

    /**
     * \brief Create an application in topology node nodeIndex.
     * The application will randomly choose a node as destination and send flows.
     */
    // DcbBaseApplication (Ptr<DcTopology> topology, uint32_t nodeIndex);

    /**
     * \brief Create an application in topology node nodeIndex destined to destIndex.
     * The application will send flows from nodeIndex to destIndex.
     * * If the destIndex is negative, the application will randomly choose a node as the
     * destination.
     */
    DcbBaseApplication();
    DcbBaseApplication(Ptr<DcTopology> topology, uint32_t nodeIndex);
    virtual ~DcbBaseApplication();

    void SetTopologyAndNode(Ptr<DcTopology> topology, uint32_t nodeIndex);

    enum ProtocolGroup
    {
        RAW_UDP,
        TCP,
        RoCEv2,
        NDP
    };

    /**
     * \brief Assign a fixed random variable stream number to the random variables
     * used by this model.
     *
     * \param stream first stream index to use
     * \return the number of stream indices assigned by this model
     */
    // int64_t AssignStreams (int64_t stream);

    void SetProtocolGroup(ProtocolGroup protoGroup);

    void SetInnerUdpProtocol(std::string innerTid);
    void SetInnerUdpProtocol(TypeId innerTid);

    struct Flow
    {
        const Time startTime;
        Time finishTime;
        const uint64_t totalBytes;
        uint64_t remainBytes;
        uint32_t destNode;
        const Ptr<Socket> socket;
        FlowIdentifier flowIdentifier;
        std::string flowTag;

        Flow(uint64_t s, Time t, uint32_t dest, Ptr<Socket> sock)
            : startTime(t),
              totalBytes(s),
              remainBytes(s),
              destNode(dest),
              socket(sock)
        {
        }

        void Dispose() // to provide a similar API as ns-3
        {
            socket->Close();
            socket->SetRecvCallback(MakeNullCallback<void, Ptr<Socket>>());
            delete this; // TODO: is it ok to suicide here?
        }
    };

    void SetEcnEnabled(bool enabled);

    void SetSendEnabled(bool enabled);
    void SetReceiveEnabled(bool enabled);

    virtual void FlowCompletes(Ptr<UdpBasedSocket> socket);
    virtual void FlowCompletes(Ptr<NdpSocket> socket);

    /**
     * \brief Callback for UnackSequence in TcpTxBuffer, in order to record the flow end time.
     * \param oldValue The old value of UnackSequence
     * \param newValue The new value of UnackSequence
     */
    static void TcpFlowEnds(Flow* flow, SequenceNumber32 oldValue, SequenceNumber32 newValue);

    typedef std::pair<std::string, Ptr<AttributeValue>> ConfigEntry_t;
    void SetCcOpsAttributes(const std::vector<ConfigEntry_t>& configs);
    void SetSocketAttributes(const std::vector<ConfigEntry_t>& configs);

    inline const ProtocolGroup GetProtoGroup() const
    {
        return m_protoGroup;
    }

    inline const bool GetSendAbility() const
    {
        return m_enableSend;
    }

    inline const Ptr<Node> GetNode() const
    {
        return m_node;
    }

    constexpr static inline const uint64_t MSS = 1000; // bytes

    class Stats
    {
      public:
        // constructor
        Stats(Ptr<DcbBaseApplication> app);
        Ptr<DcbBaseApplication> m_app;

        virtual ~Stats()
        {
        } // Make the base class polymorphic

        bool isCollected; //<! Whether the stats is collected

        uint32_t nTotalSizePkts;  //<! Data size scheduled by application
        uint64_t nTotalSizeBytes; //<! Data size scheduled by application
        uint32_t nTotalSentPkts;  //<! Data pkts sent to lower layer, including retransmission
        uint64_t nTotalSentBytes;
        uint32_t nTotalDeliverPkts; //<! Data pkts successfully delivered to peer upper layer
        uint64_t nTotalDeliverBytes;
        uint32_t nRetxCount;     //<! Number of retransmission
        Time tStart;             //<! Time of the first flow start
        Time tFinish;            //<! Time of the last flow finish
        DataRate overallRate;    //<! overall rate, calculate by total size / (first flow arrive -
                                 // last flow finish)
        std::string appFlowType; //<! The type of the application flow

        // Detailed statistics, only enabled if needed
        bool bDetailedSenderStats;
        bool bDetailedRetxStats;
        std::map<FlowIdentifier, std::shared_ptr<RoCEv2Socket::Stats>> mFlowStats;
        std::vector<std::shared_ptr<RoCEv2Socket::Stats>>
            vFlowStats; //<! The statistics of each flow

        // Collect the statistics and check if the statistics is correct
        void CollectAndCheck(std::map<Ptr<Socket>, Flow*> flows);

        // No getter for simplicity
    };

    virtual std::shared_ptr<Stats> GetStats() const = 0;

  protected:
    /**
     * \brief Create a new RoCEv2 socket using the TypeId set by SocketType attribute
     * Call this function requires m_topology set.
     *
     * \return A smart Socket pointer to a RoCEv2Socket
     *
     * \param destNode the destionation Node ID
     * \param priority the priority of the socket, default is 0
     */
    Ptr<Socket> CreateNewSocket(uint32_t destNode, uint32_t priority = 0);

    /**
     * \brief Create new socket send to the destAddr.
     */
    Ptr<Socket> CreateNewSocket(InetSocketAddress destAddr, uint32_t priority);

    /**
     * \brief Get the InetSocketAddress of the destNode.
     */
    InetSocketAddress NodeIndexToAddr(uint32_t destNode) const;

    /**
     * \brief Send a dummy packet according to m_remainBytes.
     * Raise error if packet does not sent successfully.
     * \param socket the socket to send packet.
     */
    virtual void SendNextPacket(Flow* flow);

    /**
     * \brief Send a dummy packet according to m_remainBytes.
     * Raise error if packet does not sent successfully.
     * \param socket the socket to send packet.
     * \param tags the tags to be added to the packet
     */
    virtual void SendNextPacketWithTags(Flow* flow, std::vector<std::shared_ptr<Tag>> tags);

    void SetFlowIdentifier(Flow* flow, Ptr<Socket> socket);

    DataRate m_socketLinkRate; //!< Link rate of the deice
    uint64_t m_totBytes;       //!< Total bytes sent so far
    uint32_t m_headerSize;     //!< base header bytes of a packet (Ether + IP + UDP/RoCEv2)
    uint32_t m_dataHeaderSize; //!< data header bytes of a packet (base + CC specific header)
    std::map<Ptr<Socket>, Flow*> m_flows;
    // Ptr<RoCEv2Socket> m_receiverSocket;
    Ptr<Socket> m_receiverSocket;
    Ptr<DcTopology> m_topology; //!< The topology
    uint32_t m_nodeIndex;
    ProtocolGroup m_protoGroup;                  //!< Protocol group
    std::list<Ptr<Socket>> m_acceptedSocketList; //!< the accepted sockets

    // private:
    /**
     * \brief Init fields, e.g., RNGs and m_socketLinkRate.
     */
    virtual void InitMembers() = 0;
    // inherited from Application base class.
    virtual void StartApplication(void) override; // Called at time specified by Start
    virtual void StopApplication(void) override;  // Called at time specified by Stop

    // helpers
    /**
     * \brief Cancel all pending events.
     */
    // void CancelEvents ();

    /**
     * \brief Handle a Connection Succeed event
     * \param socket the connected socket
     */
    void ConnectionSucceeded(Ptr<Socket> socket);
    /**
     * \brief Handle a Connection Failed event
     * \param socket the not connected socket
     */
    void ConnectionFailed(Ptr<Socket> socket);

    /**
     * \brief Handle a packet reception.
     *
     * This function is called by lower layers.
     *
     * \param socket the socket the packet was received to.
     */
    virtual void HandleRead(Ptr<Socket> socket);

    /** TCP Socket callback handler **/

    /**
     * \brief Handle a connection request.
     *
     * This function is called by lower layers.
     *
     * \param socket the socket the packet was received to.
     * \param from the address of the peer
     */
    virtual void HandleTcpAccept(Ptr<Socket> socket, const Address& from);
    virtual void HandleNdpAccept(Ptr<Socket> socket, const Address& from);

    virtual void HandleTcpPeerClose(Ptr<Socket> socket);

    virtual void HandleTcpPeerError(Ptr<Socket> socket);

    /**
     * \brief Find a outbound net device (i.e., not a loopback net device) for the application's
     * node.
     * \return a outbound net device (typically the only outbound net device).
     */
    Ptr<NetDevice> GetOutboundNetDevice();

    /**
     * \brief Initialize the socket line rate.
     */
    void InitSocketLineRate();

    /**
     * \brief Setup receiver socket
     */
    void SetupReceiverSocket();

    /**
     * \brief Generate traffic according to the traffic pattern.
     */
    virtual void GenerateTraffic() = 0;

    /**
     * \brief Calculate parameters of traffic.
     */
    virtual void CalcTrafficParameters() = 0;

    /**
     * \brief Set the congestion control type id and calculate the data header size.
     */
    void SetCongestionTypeId(TypeId congestionTypeId);

    std::shared_ptr<Stats> m_stats;

    bool m_enableReceive;
    bool m_enableSend;
    Ptr<Node> m_node; //!< The node the application is installed on, used when dctopo is not set
    bool m_ecnEnabled;
    uint8_t m_flowPriority; //!< The priority of the flow, 7 is the highest priority
    uint8_t m_recvPriority; //!< The priority of the receiver, 7 is the highest priority
    // bool                   m_connected;       //!< True if connected
    TypeId m_socketTid;        //!< Type of the socket used
    TypeId m_innerUdpProtocol; //!< inner-UDP protocol type id

    std::vector<ConfigEntry_t> m_socketAttributes;
    TypeId m_congestionTypeId; //!< The socket's congestion control TypeId
    std::vector<RoCEv2CongestionOps::CcOpsConfigPair_t> m_ccAttributes; //!< The attributes to be
                                                                        //!< set to the ccOps

    /// traced Callback: transmitted packets.
    TracedCallback<Ptr<const Packet>> m_txTrace;

    /// Callbacks for tracing the packet Tx events, includes source and destination addresses
    TracedCallback<Ptr<const Packet>, const Address&, const Address&> m_txTraceWithAddresses;

    /// Callback for tracing the packet Tx events, includes source, destination, the packet sent,
    /// and header
    TracedCallback<Ptr<const Packet>, const Address&, const Address&, const SeqTsSizeHeader&>
        m_txTraceWithSeqTsSize;

    TracedCallback<uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, Time, Time>
        m_flowCompleteTrace;
}; // class DcbBaseApplication

} // namespace ns3

#endif // DCB_BASE_APPLICATION_H
