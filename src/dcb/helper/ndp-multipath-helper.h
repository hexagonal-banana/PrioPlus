#ifndef NDP_MULTIPATH_HELPER_H
#define NDP_MULTIPATH_HELPER_H

#include "ns3/ipv4-address.h"
#include "ns3/ipv4-interface-container.h"
#include "ns3/ipv4.h"
#include "ns3/node-container.h"
#include "ns3/node.h"
#include "ns3/dc-topology.h"

#include <map>
#include <set>
#include <vector>

namespace ns3
{

/**
 * \ingroup dcb
 * \brief Helper class for configuring NDP multipath routing (Singleton)
 *
 * NDP requires per-packet multipath routing where each host has multiple
 * IP addresses (one per path). This helper:
 * 1. Assigns multiple IPs to each host (one per spine/core switch)
 * 2. Configures static routes so each IP is reachable only via specific path
 * 3. Provides path IP lookup for sockets
 *
 * Example topology (Leaf-Spine):
 *   Host A connected to Leaf 0
 *   Spine 0, Spine 1, Spine 2, Spine 3
 *   
 *   Host A will have 4 IPs:
 *     - IP_A_Spine0 (reachable only via Spine 0)
 *     - IP_A_Spine1 (reachable only via Spine 1)
 *     - IP_A_Spine2 (reachable only via Spine 2)
 *     - IP_A_Spine3 (reachable only via Spine 3)
 *
 * **Singleton Pattern**:
 * This class uses the singleton pattern to ensure all components share
 * the same multipath configuration. Use GetInstance() to access.
 */
class NdpMultipathHelper
{
  public:
    /**
     * \brief Get the singleton instance
     * \return Reference to the singleton instance
     */
    static NdpMultipathHelper& GetInstance();
    
    /**
     * \brief Delete copy constructor
     */
    NdpMultipathHelper(const NdpMultipathHelper&) = delete;
    
    /**
     * \brief Delete assignment operator
     */
    NdpMultipathHelper& operator=(const NdpMultipathHelper&) = delete;

    /**
     * \brief Assign multiple IP addresses to each host (one per path)
     * \param hosts Container of host nodes
     * \param numPaths Number of paths (typically number of spine switches)
     * \param baseNetwork Base network address (e.g., "10.0.0.0")
     * \param baseMask Network mask (e.g., "255.255.0.0")
     *
     * Each host will get numPaths IP addresses, e.g.:
     *   Host 0: 10.0.0.1, 10.1.0.1, 10.2.0.1, 10.3.0.1
     *   Host 1: 10.0.0.2, 10.1.0.2, 10.2.0.2, 10.3.0.2
     */
    void AssignMultipathIps(NodeContainer hosts,
                            uint32_t numPaths,
                            const char* baseNetwork = "10.0.0.0",
                            const char* baseMask = "255.255.0.0");

    /**
     * \brief Get all path IPs for a specific host
     * \param hostId Host node ID
     * \return Vector of IP addresses (one per path)
     */
    std::vector<Ipv4Address> GetPathIps(uint32_t hostId) const;

    /**
     * \brief Get all path IPs for a specific host
     * \param host Host node pointer
     * \return Vector of IP addresses (one per path)
     */
    std::vector<Ipv4Address> GetPathIps(Ptr<Node> host) const;

    /**
     * \brief Get the primary IP for a host (path 0)
     * \param hostId Host node ID
     * \return Primary IP address
     */
    Ipv4Address GetPrimaryIp(uint32_t hostId) const;

    /**
     * \brief Get number of paths configured
     * \return Number of paths
     */
    uint32_t GetNumPaths() const;

    /**
     * \brief Configure static routes for multipath
     * \param hosts Container of host nodes
     * \param leafSwitches Container of leaf switches
     * \param spineSwitches Container of spine switches
     *
     * This configures routing tables so that:
     * - Each path IP is reachable only via the corresponding spine
     * - Return path routing is properly configured
     *
     * Note: This assumes a leaf-spine topology where:
     *   - Each host is connected to one leaf
     *   - Each leaf is connected to all spines
     *   - Path i goes through spine i
     */
    void ConfigureLeafSpineRouting(NodeContainer hosts,
                                    NodeContainer leafSwitches,
                                    NodeContainer spineSwitches);

    /**
     * \brief Configure per-packet multipath routing for a 3-tier Fat-tree topology.
     *
     * Auto-detects leaf / aggregation / core switches by BFS distance from hosts.
     * For each switch, injects deterministic routes into SwitchNode::s_multipathRoutes:
     *   - /16 subnet routes for UPWARD traffic (toward a specific core switch)
     *   - Per-host exact routes for DOWNWARD traffic (toward a specific host)
     *
     * \param topology  The topology object with adjacency information
     * \param numPaths  Number of paths (must equal the number of core switches)
     *
     * \deprecated Use ConfigureAutoRouting() for generic topology support.
     */
    void ConfigureFatTreeRouting(Ptr<DcTopology> topology, uint32_t numPaths);

    /**
     * \brief Generic per-packet multipath routing configuration that auto-detects
     *        the topology structure and configures routing accordingly.
     *
     * Supports any multi-tier switched topology including:
     *   - **Star** (1 switch tier): Skips multipath (only 1 path exists)
     *   - **Spine-Leaf** (2 switch tiers): Spines are path determiners
     *   - **Fat-tree** (3 switch tiers): Core switches are path determiners
     *   - **Deep hierarchies** (4+ switch tiers): Top-tier switches are path determiners
     *
     * Algorithm:
     *   1. BFS from hosts to classify switches into tiers (tier 1 = leaf, ..., tier T = top)
     *   2. Top-tier switches become the "path determiners" (numPaths ≤ |top-tier|)
     *   3. For each top-tier node, BFS downward to compute upward next-hops at each tier
     *   4. Bottom-up subtree computation for downward routing
     *   5. Inject deterministic routes: /16 subnet (upward) + exact-host (downward)
     *
     * \param topology  The topology object with adjacency information
     * \param numPaths  Requested number of paths (clamped to top-tier switch count)
     * \return Actual number of paths configured (0 if multipath not possible)
     */
    uint32_t ConfigureAutoRouting(Ptr<DcTopology> topology, uint32_t numPaths);

    /**
     * \brief Set the base network address and mask
     * Used to pre-configure before ConfigureAutoRouting()
     */
    void SetBaseNetwork(Ipv4Address base, Ipv4Mask mask);

    /**
     * \brief Print multipath IP configuration (for debugging)
     */
    void PrintConfiguration() const;
    
    /**
     * \brief Reset/clear all multipath configuration
     * Useful for testing or reconfiguration
     */
    void Reset();

  private:
    /**
     * \brief Private constructor for singleton
     */
    NdpMultipathHelper();
    
    /**
     * \brief Private destructor
     */
    ~NdpMultipathHelper();
    /**
     * \brief Storage for host multipath IPs
     * Key: host node ID
     * Value: vector of IP addresses (one per path)
     */
    std::map<uint32_t, std::vector<Ipv4Address>> m_hostPathIps;

    /**
     * \brief Number of paths per host
     */
    uint32_t m_numPaths;

    /**
     * \brief Base network address for path subnets
     */
    Ipv4Address m_baseNetwork;

    /**
     * \brief Network mask
     */
    Ipv4Mask m_networkMask;
};

} // namespace ns3

#endif /* NDP_MULTIPATH_HELPER_H */
