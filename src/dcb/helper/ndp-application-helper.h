#ifndef NDP_APPLICATION_HELPER_H
#define NDP_APPLICATION_HELPER_H

#include "ndp-multipath-helper.h"

#include "ns3/dc-topology.h"
#include "ns3/ndp-socket.h"
#include "ns3/ndp-traffic-gen-application.h"

#include "ns3/application-container.h"
#include "ns3/node-container.h"
#include "ns3/object-factory.h"

#include <boost/json.hpp>
#include <vector>

namespace ns3
{

/**
 * \ingroup dcb
 * \brief Helper class for installing NDP applications
 *
 * This helper provides utilities to:
 * - Install NDP traffic generation applications on nodes
 * - Configure NDP sockets with multipath routing
 * - Parse NDP-specific configuration from JSON
 * - Set up NDP L4 protocol on nodes
 */
class NdpApplicationHelper
{
  public:
    NdpApplicationHelper();
    ~NdpApplicationHelper();

    /**
     * \brief Install NDP applications on specified nodes
     * \param appConfig JSON configuration for the application
     * \param topology Datacenter topology
     * \return Container of installed applications
     *
     * This function:
     * 1. Parses NDP-specific configuration (firstWindowSize, receiverLinkRate)
     * 2. Parses multipath configuration (numPaths, pathSelection)
     * 3. Installs NDP L4 protocol on nodes if not already installed
     * 4. Creates NDP traffic generation applications
     * 5. Configures multipath routing
     */
    ApplicationContainer InstallNdpApplications(const boost::json::object& appConfig,
                                                Ptr<DcTopology> topology);

    /**
     * \brief Configure multipath routing for NDP
     * \param multipathConfig JSON multipath configuration
     * \param hosts Container of host nodes
     * \param switches Container of switch nodes
     * \param topology Topology (required for fat-tree auto-detection)
     *
     * Sets up:
     * - Multiple IP addresses per host (one per path)
     * - Deterministic routing (fat-tree: subnet-based; leaf-spine: static)
     * - Path IP lookup for sockets
     */
    void ConfigureMultipathRouting(const boost::json::object& multipathConfig,
                                    NodeContainer hosts,
                                    NodeContainer switches,
                                    Ptr<DcTopology> topology = nullptr);

    /**
     * \brief Set the topology
     * \param topology Datacenter topology
     */
    void SetTopology(Ptr<DcTopology> topology);

  private:
    /**
     * \brief Parse NDP-specific application configuration
     * \param appConfig JSON application configuration
     * \param app NDP application to configure
     */
    void ParseNdpConfig(const boost::json::object& appConfig,
                        Ptr<NdpTrafficGenApplication> app);

    /**
     * \brief Parse multipath configuration
     * \param multipathConfig JSON multipath configuration
     * \param app NDP application to configure
     */
    void ParseMultipathConfig(const boost::json::object& multipathConfig,
                              Ptr<NdpTrafficGenApplication> app);

    /**
     * \brief Install NDP L4 protocol on a node if not already installed
     * \param node Node to install protocol on
     * \return Pointer to the NDP L4 protocol
     */
    Ptr<NdpL4Protocol> EnsureNdpL4Protocol(Ptr<Node> node);

    /**
     * \brief Parse node specification from JSON (e.g., "[0:3]", "all", "random")
     * \param nodeSpec Node specification string
     * \param topology Datacenter topology
     * \return Vector of node indices
     */
    std::vector<uint32_t> ParseNodeSpec(const std::string& nodeSpec,
                                          Ptr<DcTopology> topology);

    /**
     * \brief Get traffic pattern from string
     * \param patternStr Pattern string (e.g., "SEND_ONCE", "CONTINUOUS", "CDF")
     * \return Traffic pattern enum
     */
    NdpTrafficGenApplication::TrafficPattern GetTrafficPattern(const std::string& patternStr);

    /**
     * \brief Parse CDF configuration and set it on the application
     * \param appConfig Top-level application config (contains "cdf" block)
     * \param app NDP application to configure
     * \param linkRate Host link rate
     * \param topology Topology for random dest
     * \param nodeIndex This node's host index
     * \param stopTime Stop time for CDF generation
     */
    void SetupCdfTraffic(const boost::json::object& appConfig,
                         Ptr<NdpTrafficGenApplication> app,
                         DataRate linkRate,
                         Ptr<DcTopology> topology,
                         uint32_t nodeIndex,
                         Time stopTime);

    /**
     * \brief Read and construct CDF from file
     * \param filename CDF file path
     * \return Vector of (flowSize, probability) pairs
     */
    static std::unique_ptr<NdpTrafficGenApplication::TraceCdf>
    ConstructCdfFromFile(const std::string& filename);

    Ptr<DcTopology> m_topology;          ///< Datacenter topology
    ObjectFactory m_appFactory;           ///< Application factory
};

} // namespace ns3

#endif /* NDP_APPLICATION_HELPER_H */
