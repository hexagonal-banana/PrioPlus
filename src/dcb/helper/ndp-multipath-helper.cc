#include "ndp-multipath-helper.h"

#include "ns3/channel.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/ipv4-list-routing-helper.h"
#include "ns3/ipv4-list-routing.h"
#include "ns3/ipv4-static-routing-helper.h"
#include "ns3/ipv4-static-routing.h"
#include "ns3/log.h"
#include "ns3/switch-node.h"

#include <queue>
#include <algorithm>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("NdpMultipathHelper");

NdpMultipathHelper&
NdpMultipathHelper::GetInstance()
{
    static NdpMultipathHelper instance;
    return instance;
}

NdpMultipathHelper::NdpMultipathHelper()
    : m_numPaths(0),
      m_baseNetwork("10.0.0.0"),
      m_networkMask("255.255.0.0")
{
    NS_LOG_FUNCTION(this);
    NS_LOG_INFO("✅ NdpMultipathHelper singleton instance created");
}

NdpMultipathHelper::~NdpMultipathHelper()
{
    NS_LOG_FUNCTION(this);
}

void
NdpMultipathHelper::Reset()
{
    NS_LOG_FUNCTION(this);
    m_hostPathIps.clear();
    m_numPaths = 0;
    m_baseNetwork = Ipv4Address("10.0.0.0");
    m_networkMask = Ipv4Mask("255.255.0.0");
    NS_LOG_INFO("✅ NdpMultipathHelper configuration reset");
}

void
NdpMultipathHelper::AssignMultipathIps(NodeContainer hosts,
                                       uint32_t numPaths,
                                       const char* baseNetwork,
                                       const char* baseMask)
{
    NS_LOG_FUNCTION(this << hosts.GetN() << numPaths << baseNetwork << baseMask);

    m_numPaths = numPaths;
    m_baseNetwork = Ipv4Address(baseNetwork);
    m_networkMask = Ipv4Mask(baseMask);

    // Clear existing configuration
    m_hostPathIps.clear();

    // Assign IPs to each host
    // Note: This is a simplified implementation for demonstration
    // In a real leaf-spine multipath setup, each path would require:
    // 1. Separate IP addresses for each host (one per path)
    // 2. Routing table configuration to direct each IP through a specific spine
    // 3. ECMP disabled to ensure deterministic routing
    //
    // For now, we use a simpler approach: all paths share the same destination IP,
    // and path selection is maintained at the transport layer for monitoring purposes
    
    for (uint32_t hostIdx = 0; hostIdx < hosts.GetN(); hostIdx++)
    {
        Ptr<Node> host = hosts.Get(hostIdx);
        uint32_t hostId = host->GetId();
        std::vector<Ipv4Address> pathIps;

        // Get IPv4 object for this host
        Ptr<Ipv4> ipv4 = host->GetObject<Ipv4>();
        if (!ipv4)
        {
            NS_LOG_WARN("Host " << hostId << " does not have IPv4 installed, skipping");
            continue;
        }

        // Get the primary IP address from the first non-loopback interface
        Ipv4Address primaryIp;
        if (ipv4->GetNInterfaces() > 1)
        {
            primaryIp = ipv4->GetAddress(1, 0).GetLocal();
        }
        else
        {
            NS_LOG_WARN("Host " << hostId << " does not have a non-loopback interface");
            continue;
        }

        // ===== TRUE PER-PACKET MULTIPATH IMPLEMENTATION =====
        // Create one distinct IP address per path
        // Each IP maps to a specific physical path through the topology
        
        for (uint32_t pathIdx = 0; pathIdx < numPaths; pathIdx++)
        {
            // Generate path-specific IP: 10.(pathIdx+1).0.hostId
            // Using (pathIdx+1) to AVOID the 10.0.0.0/16 subnet which is
            // already occupied by primary IPs from GlobalRouter.
            // Example: Base = 10.0.0.0, 4 paths
            //   Host 0: [10.1.0.1, 10.2.0.1, 10.3.0.1, 10.4.0.1]
            //   Host 1: [10.1.0.2, 10.2.0.2, 10.3.0.2, 10.4.0.2]
            
            uint32_t baseAddr = m_baseNetwork.Get();
            uint32_t pathOffset = (pathIdx + 1) << 16;  // 10.1, 10.2, ... (avoids 10.0)
            uint32_t hostOffset = (hostIdx + 1);        // Host ID in 4th octet
            
            Ipv4Address pathIp;
            pathIp.Set(baseAddr + pathOffset + hostOffset);
            pathIps.push_back(pathIp);
            
            NS_LOG_INFO("✅ Host " << hostId << " path " << pathIdx << ": " << pathIp);
        }

        m_hostPathIps[hostId] = pathIps;
        
        NS_LOG_INFO("✅✅ Saved " << pathIps.size() << " path IPs for host " << hostId 
                    << " (map size now: " << m_hostPathIps.size() << ")");
        
        // Now we need to actually configure these IPs on the interface
        // For path 0, the IP might already be configured by topology setup
        // For other paths, we need to add them as additional addresses
        
        for (uint32_t pathIdx = 0; pathIdx < numPaths; pathIdx++)
        {
            Ipv4Address pathIp = pathIps[pathIdx];
            
            // Check if this IP already exists
            bool ipExists = false;
            for (uint32_t i = 0; i < ipv4->GetNAddresses(1); i++)
            {
                if (ipv4->GetAddress(1, i).GetLocal() == pathIp)
                {
                    ipExists = true;
                    NS_LOG_DEBUG("IP " << pathIp << " already exists on interface 1");
                    break;
                }
            }
            
            // Add IP if it doesn't exist
            if (!ipExists)
            {
                Ipv4InterfaceAddress ifaceAddr(pathIp, m_networkMask);
                bool added = ipv4->AddAddress(1, ifaceAddr);
                if (added)
                {
                    NS_LOG_INFO("✅ Added IP " << pathIp << " to host " << hostId 
                                << " interface 1 (path " << pathIdx << ")");
                }
                else
                {
                    NS_LOG_WARN("❌ Failed to add IP " << pathIp << " to host " << hostId);
                }
            }
        }
    }

    NS_LOG_INFO("Assigned " << numPaths << " path IPs to " << hosts.GetN() << " hosts");
}

std::vector<Ipv4Address>
NdpMultipathHelper::GetPathIps(uint32_t hostId) const
{
    NS_LOG_INFO("GetPathIps called for hostId=" << hostId 
                << ", map size=" << m_hostPathIps.size());
    
    auto it = m_hostPathIps.find(hostId);
    if (it != m_hostPathIps.end())
    {
        NS_LOG_INFO("✅ Found " << it->second.size() << " path IPs for host " << hostId);
        return it->second;
    }

    NS_LOG_DEBUG("No path IPs found for host " << hostId << " (using single-path mode)");
    return std::vector<Ipv4Address>();
}

std::vector<Ipv4Address>
NdpMultipathHelper::GetPathIps(Ptr<Node> host) const
{
    return GetPathIps(host->GetId());
}

Ipv4Address
NdpMultipathHelper::GetPrimaryIp(uint32_t hostId) const
{
    std::vector<Ipv4Address> ips = GetPathIps(hostId);
    if (!ips.empty())
    {
        return ips[0];
    }

    NS_LOG_DEBUG("No path IPs found for host " << hostId << " (using single-path mode)");
    return Ipv4Address("0.0.0.0");
}

uint32_t
NdpMultipathHelper::GetNumPaths() const
{
    return m_numPaths;
}

void
NdpMultipathHelper::ConfigureLeafSpineRouting(NodeContainer hosts,
                                               NodeContainer leafSwitches,
                                               NodeContainer spineSwitches)
{
    NS_LOG_FUNCTION(this << hosts.GetN() << leafSwitches.GetN() << spineSwitches.GetN());

    if (spineSwitches.GetN() != m_numPaths)
    {
        NS_LOG_WARN("Number of spine switches (" << spineSwitches.GetN()
                                                  << ") != number of paths (" << m_numPaths << ")");
    }
    
    // ===== TRUE PER-PACKET MULTIPATH ROUTING CONFIGURATION =====
    // Topology assumptions:
    // - Each host connects to one leaf switch
    // - Each leaf connects to all spine switches
    // - Path i goes through spine switch i
    // - We use different /16 subnets for each path:
    //   * 10.0.0.0/16 → path 0 → spine 0
    //   * 10.1.0.0/16 → path 1 → spine 1
    //   * 10.2.0.0/16 → path 2 → spine 2
    //   * 10.3.0.0/16 → path 3 → spine 3
    
    Ipv4StaticRoutingHelper staticRoutingHelper;
    
    // === 1. Configure Hosts ===
    // Hosts just need to send everything to their connected leaf switch
    for (uint32_t i = 0; i < hosts.GetN(); i++)
    {
        Ptr<Node> host = hosts.Get(i);
        Ptr<Ipv4> ipv4 = host->GetObject<Ipv4>();
        if (!ipv4) continue;
        
        Ptr<Ipv4StaticRouting> staticRouting = staticRoutingHelper.GetStaticRouting(ipv4);
        if (!staticRouting)
        {
            NS_LOG_WARN("❌ Host " << host->GetId() << " has no static routing");
            continue;
        }
        
        // Add a default route via interface 1 (connected to leaf)
        // This sends all traffic to the leaf switch
        staticRouting->SetDefaultRoute(Ipv4Address("0.0.0.0"), 1);
        
        NS_LOG_INFO("✅ Host " << host->GetId() << ": Default route → interface 1 (to leaf)");
    }
    
    // === 2. Configure Leaf Switches ===
    // Each leaf routes different /16 subnets to different spines
    for (uint32_t leafIdx = 0; leafIdx < leafSwitches.GetN(); leafIdx++)
    {
        Ptr<Node> leaf = leafSwitches.Get(leafIdx);
        Ptr<Ipv4> ipv4 = leaf->GetObject<Ipv4>();
        if (!ipv4) continue;
        
        Ptr<Ipv4StaticRouting> staticRouting = staticRoutingHelper.GetStaticRouting(ipv4);
        if (!staticRouting)
        {
            NS_LOG_WARN("❌ Leaf " << leaf->GetId() << " has no static routing");
            continue;
        }
        
        // For each path subnet, route to the corresponding spine
        for (uint32_t pathIdx = 0; pathIdx < m_numPaths && pathIdx < spineSwitches.GetN(); pathIdx++)
        {
            // Calculate the subnet for this path: 10.pathIdx.0.0/16
            uint32_t baseAddr = m_baseNetwork.Get();
            uint32_t pathOffset = pathIdx << 16;
            Ipv4Address pathSubnet(baseAddr + pathOffset);
            Ipv4Mask subnetMask("255.255.0.0");  // /16
            
            // Assumption about interface layout on leaf:
            // - Interface 0: loopback
            // - Interface 1, 2, ...: to hosts (downlinks)
            // - Interface N, N+1, ...: to spines (uplinks)
            // 
            // For a leaf with hostsPerLeaf hosts:
            // - Interfaces 1 to hostsPerLeaf: to hosts
            // - Interfaces (hostsPerLeaf+1) to (hostsPerLeaf+numSpines): to spines
            //
            // Simplified: assume interface (2 + pathIdx) connects to spine pathIdx
            uint32_t spineInterface = 2 + pathIdx;
            
            if (spineInterface < ipv4->GetNInterfaces())
            {
                staticRouting->AddNetworkRouteTo(pathSubnet, subnetMask, spineInterface);
                NS_LOG_INFO("✅ Leaf " << leaf->GetId() << ": " << pathSubnet << "/" << subnetMask
                            << " → interface " << spineInterface << " (to spine " << pathIdx << ")");
            }
            else
            {
                NS_LOG_WARN("❌ Leaf " << leaf->GetId() << " doesn't have interface " 
                            << spineInterface << " for path " << pathIdx);
            }
        }
        
        // Add default route to first spine (for any unmatched traffic)
        if (ipv4->GetNInterfaces() > 2)
        {
            staticRouting->SetDefaultRoute(Ipv4Address("0.0.0.0"), 2);
            NS_LOG_DEBUG("Leaf " << leaf->GetId() << ": Default route → interface 2");
        }
    }
    
    // === 3. Configure Spine Switches ===
    // Spines route back to leaves (all path subnets)
    for (uint32_t spineIdx = 0; spineIdx < spineSwitches.GetN(); spineIdx++)
    {
        Ptr<Node> spine = spineSwitches.Get(spineIdx);
        Ptr<Ipv4> ipv4 = spine->GetObject<Ipv4>();
        if (!ipv4) continue;
        
        Ptr<Ipv4StaticRouting> staticRouting = staticRoutingHelper.GetStaticRouting(ipv4);
        if (!staticRouting)
        {
            NS_LOG_WARN("❌ Spine " << spine->GetId() << " has no static routing");
            continue;
        }
        
        // Route all path subnets back through the connected leaves
        for (uint32_t pathIdx = 0; pathIdx < m_numPaths; pathIdx++)
        {
            uint32_t baseAddr = m_baseNetwork.Get();
            uint32_t pathOffset = pathIdx << 16;
            Ipv4Address pathSubnet(baseAddr + pathOffset);
            Ipv4Mask subnetMask("255.255.0.0");
            
            // Spines are connected to all leaves
            // Assumption: interface (1 + leafIdx) connects to leaf leafIdx
            for (uint32_t leafIdx = 0; leafIdx < leafSwitches.GetN(); leafIdx++)
            {
                uint32_t leafInterface = 1 + leafIdx;
                
                if (leafInterface < ipv4->GetNInterfaces())
                {
                    staticRouting->AddNetworkRouteTo(pathSubnet, subnetMask, leafInterface);
                    NS_LOG_DEBUG("Spine " << spine->GetId() << ": " << pathSubnet << "/" << subnetMask
                                << " → interface " << leafInterface << " (to leaf " << leafIdx << ")");
                }
            }
        }
    }
    
    NS_LOG_INFO("✅✅✅ Configured TRUE per-packet multipath routing:");
    NS_LOG_INFO("    - " << hosts.GetN() << " hosts");
    NS_LOG_INFO("    - " << leafSwitches.GetN() << " leaves");
    NS_LOG_INFO("    - " << spineSwitches.GetN() << " spines");
    NS_LOG_INFO("    - " << m_numPaths << " paths");
    NS_LOG_INFO("    - Sender controls each packet's path via destination IP selection");
}

// ============================================================================
// ConfigureFatTreeRouting — auto-detect 3-tier fat-tree and inject per-packet
// multipath routes into SwitchNode routing tables.
// ============================================================================
void
NdpMultipathHelper::ConfigureFatTreeRouting(Ptr<DcTopology> topology, uint32_t numPaths)
{
    NS_LOG_FUNCTION(this << numPaths);

    const uint32_t N = topology->GetNNodes();

    // ── Step 0: Build adjacency & device-index maps ──────────────────────
    // adj[nodeA] = { nodeB, nodeC, ... }
    // devMap[nodeA][nodeB] = devIdx on nodeA that connects to nodeB
    std::vector<std::set<uint32_t>> adj(N);
    std::map<uint32_t, std::map<uint32_t, int>> devMap;

    for (uint32_t i = 0; i < N; i++)
    {
        Ptr<Node> node = topology->GetNode(i).nodePtr;
        for (uint32_t d = 1; d < node->GetNDevices(); d++) // skip loopback
        {
            Ptr<NetDevice> dev = node->GetDevice(d);
            Ptr<Channel> ch = dev->GetChannel();
            if (!ch) continue;
            for (uint32_t j = 0; j < ch->GetNDevices(); j++)
            {
                Ptr<NetDevice> otherDev = ch->GetDevice(j);
                if (otherDev != dev)
                {
                    uint32_t neighborId = otherDev->GetNode()->GetId();
                    adj[i].insert(neighborId);
                    devMap[i][neighborId] = static_cast<int>(d);
                }
            }
        }
    }

    // ── Step 1: Classify switches by BFS distance from hosts ─────────────
    // distance 1 = leaf, distance 2 = agg, distance 3 = core
    std::vector<int> tier(N, -1); // -1=host, 1=leaf, 2=agg, 3=core
    for (uint32_t i = 0; i < N; i++)
    {
        if (topology->IsHost(i))
        {
            tier[i] = 0; // host
        }
    }

    // BFS from ALL hosts simultaneously
    std::queue<uint32_t> bfsQueue;
    for (uint32_t i = 0; i < N; i++)
    {
        if (tier[i] == 0) bfsQueue.push(i);
    }
    while (!bfsQueue.empty())
    {
        uint32_t cur = bfsQueue.front();
        bfsQueue.pop();
        for (uint32_t nb : adj[cur])
        {
            if (tier[nb] == -1) // unvisited switch
            {
                tier[nb] = tier[cur] + 1;
                bfsQueue.push(nb);
            }
        }
    }

    // Collect nodes by tier
    std::set<uint32_t> leafNodes, aggNodes, coreNodes, hostNodes;
    for (uint32_t i = 0; i < N; i++)
    {
        switch (tier[i])
        {
        case 0: hostNodes.insert(i); break;
        case 1: leafNodes.insert(i); break;
        case 2: aggNodes.insert(i);  break;
        case 3: coreNodes.insert(i); break;
        default: break;
        }
    }

    // Validate
    if (coreNodes.size() != numPaths)
    {
        NS_LOG_WARN("Fat-tree detected " << coreNodes.size()
                    << " core switches but numPaths=" << numPaths
                    << ". Using min(" << coreNodes.size() << "," << numPaths << ").");
        numPaths = std::min(numPaths, static_cast<uint32_t>(coreNodes.size()));
    }

    // Assign core indices (sorted by node ID for determinism)
    std::vector<uint32_t> coreList(coreNodes.begin(), coreNodes.end());
    std::sort(coreList.begin(), coreList.end());
    std::map<uint32_t, uint32_t> coreNodeToIdx; // coreNodeId → coreIndex (0..K-1)
    for (uint32_t i = 0; i < coreList.size(); i++)
    {
        coreNodeToIdx[coreList[i]] = i;
    }

    NS_LOG_INFO("NDP fat-tree multipath config: hosts=" << hostNodes.size()
                << ", leaves=" << leafNodes.size()
                << ", aggs=" << aggNodes.size()
                << ", cores=" << coreNodes.size());

    // ── Step 2: Compute subtree (downward-reachable hosts) per switch ────
    // subtree[switchId] = { hostId1, hostId2, ... }
    std::map<uint32_t, std::set<uint32_t>> subtree;

    // Leaf subtree: directly connected hosts
    for (uint32_t leaf : leafNodes)
    {
        for (uint32_t nb : adj[leaf])
        {
            if (hostNodes.count(nb)) subtree[leaf].insert(nb);
        }
    }
    // Agg subtree: union of connected leaves' subtrees
    for (uint32_t agg : aggNodes)
    {
        for (uint32_t nb : adj[agg])
        {
            if (leafNodes.count(nb))
            {
                subtree[agg].insert(subtree[nb].begin(), subtree[nb].end());
            }
        }
    }
    // Core subtree: all hosts
    for (uint32_t core : coreNodes)
    {
        subtree[core] = hostNodes;
    }

    // ── Step 3: For each agg, find which cores it directly connects to ───
    // aggToCores[aggId] = { coreIdx0, coreIdx1, ... }
    std::map<uint32_t, std::set<uint32_t>> aggToCoreIdx;
    for (uint32_t agg : aggNodes)
    {
        for (uint32_t nb : adj[agg])
        {
            if (coreNodes.count(nb))
            {
                aggToCoreIdx[agg].insert(coreNodeToIdx[nb]);
            }
        }
    }

    // ── Step 4: For each leaf, find which agg leads to which core ────────
    // leafCoreToAgg[leafId][coreIdx] = aggNodeId
    std::map<uint32_t, std::map<uint32_t, uint32_t>> leafCoreToAgg;
    for (uint32_t leaf : leafNodes)
    {
        for (uint32_t nb : adj[leaf])
        {
            if (aggNodes.count(nb))
            {
                for (uint32_t ci : aggToCoreIdx[nb])
                {
                    leafCoreToAgg[leaf][ci] = nb;
                }
            }
        }
    }

    // ── Step 5: Inject routes ────────────────────────────────────────────
    uint32_t baseAddr = m_baseNetwork.Get();
    uint32_t totalRoutes = 0;

    // Collect all host node IDs in sorted order for IP mapping
    std::vector<uint32_t> hostList(hostNodes.begin(), hostNodes.end());
    std::sort(hostList.begin(), hostList.end());
    // hostId → hostIndex (0-based) for IP calculation
    std::map<uint32_t, uint32_t> hostToIdx;
    for (uint32_t i = 0; i < hostList.size(); i++)
    {
        hostToIdx[hostList[i]] = i;
    }

    // --- Leaf switches ---
    for (uint32_t leaf : leafNodes)
    {
        // (a) Upward /16 subnet routes: for each core, route to the agg leading there
        for (uint32_t ci = 0; ci < numPaths; ci++)
        {
            auto it = leafCoreToAgg[leaf].find(ci);
            if (it == leafCoreToAgg[leaf].end()) continue;
            uint32_t aggId = it->second;
            int devIdx = devMap[leaf][aggId];
            uint32_t subnetIp = baseAddr + ((ci + 1) << 16); // 10.(ci+1).0.0
            SwitchNode::AddMultipathRoute(leaf, subnetIp, devIdx);
            totalRoutes++;
        }
        // (b) Downward exact routes: for LOCAL hosts only
        for (uint32_t hostId : subtree[leaf])
        {
            int devIdx = devMap[leaf][hostId];
            for (uint32_t ci = 0; ci < numPaths; ci++)
            {
                uint32_t ip = baseAddr + ((ci + 1) << 16) + (hostToIdx[hostId] + 1);
                SwitchNode::AddMultipathRoute(leaf, ip, devIdx);
                totalRoutes++;
            }
        }
    }

    // --- Agg switches ---
    for (uint32_t agg : aggNodes)
    {
        // (a) Upward /16 subnet routes: for each core this agg connects to
        for (uint32_t nb : adj[agg])
        {
            if (!coreNodes.count(nb)) continue;
            uint32_t ci = coreNodeToIdx[nb];
            int devIdx = devMap[agg][nb];
            uint32_t subnetIp = baseAddr + ((ci + 1) << 16);
            SwitchNode::AddMultipathRoute(agg, subnetIp, devIdx);
            totalRoutes++;
        }
        // (b) Downward exact routes: for hosts in this agg's subtree
        for (uint32_t hostId : subtree[agg])
        {
            // Find the leaf that connects to this host
            uint32_t targetLeaf = 0;
            for (uint32_t nb : adj[agg])
            {
                if (leafNodes.count(nb) && subtree[nb].count(hostId))
                {
                    targetLeaf = nb;
                    break;
                }
            }
            int devIdx = devMap[agg][targetLeaf];
            for (uint32_t ci = 0; ci < numPaths; ci++)
            {
                uint32_t ip = baseAddr + ((ci + 1) << 16) + (hostToIdx[hostId] + 1);
                SwitchNode::AddMultipathRoute(agg, ip, devIdx);
                totalRoutes++;
            }
        }
    }

    // --- Core switches ---
    for (uint32_t core : coreNodes)
    {
        // Core only needs downward routes (no upward routing needed)
        // For each host, find which agg (connected to this core) leads to that host
        for (uint32_t hostId : hostNodes)
        {
            // Find the agg connected to this core that has hostId in its subtree
            uint32_t targetAgg = 0;
            bool found = false;
            for (uint32_t nb : adj[core])
            {
                if (aggNodes.count(nb) && subtree[nb].count(hostId))
                {
                    targetAgg = nb;
                    found = true;
                    break;
                }
            }
            if (!found) continue;
            int devIdx = devMap[core][targetAgg];
            for (uint32_t ci = 0; ci < numPaths; ci++)
            {
                uint32_t ip = baseAddr + ((ci + 1) << 16) + (hostToIdx[hostId] + 1);
                SwitchNode::AddMultipathRoute(core, ip, devIdx);
                totalRoutes++;
            }
        }
    }

    NS_LOG_INFO("NDP fat-tree multipath routes injected: total=" << totalRoutes
                << ", switches=" << (leafNodes.size() + aggNodes.size() + coreNodes.size())
                << ", subnet scheme=10.(coreIdx+1).0.0/16");
}

// ============================================================================
// ConfigureAutoRouting — generic multi-tier topology auto-detection and
// per-packet multipath route injection.
// ============================================================================
uint32_t
NdpMultipathHelper::ConfigureAutoRouting(Ptr<DcTopology> topology, uint32_t numPaths)
{
    NS_LOG_FUNCTION(this << numPaths);

    const uint32_t N = topology->GetNNodes();

    // ── Step 0: Build adjacency & device-index maps ──────────────────────
    std::vector<std::set<uint32_t>> adj(N);
    std::map<uint32_t, std::map<uint32_t, int>> devMap;

    for (uint32_t i = 0; i < N; i++)
    {
        Ptr<Node> node = topology->GetNode(i).nodePtr;
        for (uint32_t d = 1; d < node->GetNDevices(); d++) // skip loopback
        {
            Ptr<NetDevice> dev = node->GetDevice(d);
            Ptr<Channel> ch = dev->GetChannel();
            if (!ch) continue;
            for (uint32_t j = 0; j < ch->GetNDevices(); j++)
            {
                Ptr<NetDevice> otherDev = ch->GetDevice(j);
                if (otherDev != dev)
                {
                    uint32_t neighborId = otherDev->GetNode()->GetId();
                    adj[i].insert(neighborId);
                    devMap[i][neighborId] = static_cast<int>(d);
                }
            }
        }
    }

    // ── Step 1: BFS from hosts to classify nodes into tiers ──────────────
    // tier 0 = host, tier 1 = first switch layer, ..., tier T = top switch layer
    std::vector<int> tier(N, -1);
    for (uint32_t i = 0; i < N; i++)
    {
        if (topology->IsHost(i))
        {
            tier[i] = 0;
        }
    }

    std::queue<uint32_t> bfsQueue;
    for (uint32_t i = 0; i < N; i++)
    {
        if (tier[i] == 0) bfsQueue.push(i);
    }
    while (!bfsQueue.empty())
    {
        uint32_t cur = bfsQueue.front();
        bfsQueue.pop();
        for (uint32_t nb : adj[cur])
        {
            if (tier[nb] == -1)
            {
                tier[nb] = tier[cur] + 1;
                bfsQueue.push(nb);
            }
        }
    }

    // ── Step 2: Group nodes by tier and find maxTier ─────────────────────
    int maxTier = 0;
    std::set<uint32_t> hostNodes;
    std::map<int, std::set<uint32_t>> tierNodes; // tier -> set of node IDs

    for (uint32_t i = 0; i < N; i++)
    {
        if (tier[i] < 0) continue; // unreachable node
        tierNodes[tier[i]].insert(i);
        if (tier[i] == 0) hostNodes.insert(i);
        if (tier[i] > maxTier) maxTier = tier[i];
    }

    // ── Step 3: Edge cases ──────────────────────────────────────────────
    if (maxTier < 1)
    {
        NS_LOG_WARN("Topology has no switches; multipath not applicable.");
        return 0;
    }

    std::set<uint32_t>& topTierNodes = tierNodes[maxTier];

    if (topTierNodes.size() <= 1)
    {
        NS_LOG_INFO("Topology has only " << topTierNodes.size()
                    << " top-tier switch(es) (maxTier=" << maxTier
                    << "); multipath not needed.");
        return topTierNodes.size();
    }

    // Clamp numPaths to actual top-tier switch count
    if (numPaths > topTierNodes.size())
    {
        NS_LOG_WARN("Requested numPaths=" << numPaths
                    << " but only " << topTierNodes.size()
                    << " top-tier switches. Clamping.");
        numPaths = static_cast<uint32_t>(topTierNodes.size());
    }

    // Build sorted top-tier list and index mapping
    std::vector<uint32_t> topList(topTierNodes.begin(), topTierNodes.end());
    std::sort(topList.begin(), topList.end());
    std::map<uint32_t, uint32_t> topNodeToIdx;
    for (uint32_t i = 0; i < topList.size(); i++)
    {
        topNodeToIdx[topList[i]] = i;
    }

    NS_LOG_INFO("NDP auto multipath config: tiers=" << maxTier
                << ", hosts=" << hostNodes.size()
                << ", switches=" << (N - hostNodes.size())
                << ", top-tier=" << topTierNodes.size()
                << ", paths=" << numPaths);

    // ── Step 4: Compute subtrees bottom-up ──────────────────────────────
    // subtree[switchId] = set of host IDs reachable downward
    std::map<uint32_t, std::set<uint32_t>> subtree;

    for (int t = 1; t <= maxTier; t++)
    {
        for (uint32_t node : tierNodes[t])
        {
            if (t == 1)
            {
                // Tier 1 (leaf): subtree = directly connected hosts
                for (uint32_t nb : adj[node])
                {
                    if (tier[nb] == 0) subtree[node].insert(nb);
                }
            }
            else
            {
                // Higher tiers: union of connected lower-tier subtrees
                for (uint32_t nb : adj[node])
                {
                    if (tier[nb] == t - 1)
                    {
                        subtree[node].insert(subtree[nb].begin(), subtree[nb].end());
                    }
                }
            }
        }
    }

    // ── Step 5: Compute upward next-hops via BFS from each top-tier node ─
    // upwardNextHop[switchId][topIdx] = neighbor node ID that leads toward
    //                                   top-tier node topList[topIdx]
    std::map<uint32_t, std::map<uint32_t, uint32_t>> upwardNextHop;

    for (uint32_t ci = 0; ci < numPaths; ci++)
    {
        uint32_t topNode = topList[ci];

        // BFS downward from topNode
        std::queue<uint32_t> q;
        std::set<uint32_t> visited;
        q.push(topNode);
        visited.insert(topNode);

        while (!q.empty())
        {
            uint32_t cur = q.front();
            q.pop();

            for (uint32_t nb : adj[cur])
            {
                if (visited.count(nb)) continue;
                if (tier[nb] >= tier[cur]) continue; // only go downward
                if (tier[nb] == 0) continue;         // skip hosts

                // nb is a switch at a lower tier; cur is its upward next-hop
                // toward topNode
                upwardNextHop[nb][ci] = cur;
                visited.insert(nb);
                q.push(nb);
            }
        }
    }

    // ── Step 6: Configure host routing ─────────────────────────────────
    // Hosts have a single uplink (interface 1) to their leaf switch.
    // GlobalRouter only creates routes for primary IPs (10.0.0.X).
    // We must add static routes for multipath subnets (10.1.0.0/16, ...)
    // so that NdpL4Protocol::Send() → RouteOutput() can find a route
    // when the destination IP is a multipath address.
    {
        Ipv4StaticRoutingHelper staticRoutingHelper;
        uint32_t hostRoutesAdded = 0;
        uint32_t hostSkipped = 0;
        for (uint32_t hostId : hostNodes)
        {
            Ptr<Node> host = topology->GetNode(hostId).nodePtr;
            Ptr<Ipv4> ipv4 = host->GetObject<Ipv4>();
            if (!ipv4 || ipv4->GetNInterfaces() < 2)
            {
                hostSkipped++;
                continue;
            }

            Ptr<Ipv4StaticRouting> sr = staticRoutingHelper.GetStaticRouting(ipv4);
            if (!sr)
            {
                // StaticRouting not found in the routing stack.
                // Create one and inject it into Ipv4ListRouting with high priority
                // so that multipath subnet routes take precedence over GlobalRouting.
                Ptr<Ipv4RoutingProtocol> rp = ipv4->GetRoutingProtocol();
                Ptr<Ipv4ListRouting> lr = DynamicCast<Ipv4ListRouting>(rp);
                if (lr)
                {
                    sr = CreateObject<Ipv4StaticRouting>();
                    lr->AddRoutingProtocol(sr, 100); // high priority
                    NS_LOG_INFO("Host " << hostId << ": created Ipv4StaticRouting (priority 100)");
                }
                else
                {
                    NS_LOG_WARN("Host " << hostId << ": cannot add static routes"
                                " (no ListRouting, rp="
                                << (rp ? rp->GetInstanceTypeId().GetName() : "null") << ")");
                    hostSkipped++;
                    continue;
                }
            }

            // Add a network route for each multipath /16 subnet → interface 1
            uint32_t baseIp = m_baseNetwork.Get();
            for (uint32_t ci = 0; ci < numPaths; ci++)
            {
                Ipv4Address subnet(baseIp + ((ci + 1) << 16));
                sr->AddNetworkRouteTo(subnet, Ipv4Mask("255.255.0.0"), 1);
                hostRoutesAdded++;
            }
        }
        NS_LOG_INFO("Host static routes added=" << hostRoutesAdded
                    << " (multipath subnets -> interface 1), skipped hosts=" << hostSkipped);
    }

    // ── Step 7: Inject switch routes ────────────────────────────────────
    uint32_t baseAddr = m_baseNetwork.Get();
    uint32_t totalRoutes = 0;

    // Host index mapping for IP calculation
    std::vector<uint32_t> hostList(hostNodes.begin(), hostNodes.end());
    std::sort(hostList.begin(), hostList.end());
    std::map<uint32_t, uint32_t> hostToIdx;
    for (uint32_t i = 0; i < hostList.size(); i++)
    {
        hostToIdx[hostList[i]] = i;
    }

    // --- All switch tiers ---
    for (int t = 1; t <= maxTier; t++)
    {
        for (uint32_t sw : tierNodes[t])
        {
            // (a) Upward /16 subnet routes (only for non-top-tier switches)
            if (t < maxTier)
            {
                for (uint32_t ci = 0; ci < numPaths; ci++)
                {
                    auto it = upwardNextHop[sw].find(ci);
                    if (it == upwardNextHop[sw].end()) continue;

                    uint32_t nextHopNode = it->second;
                    int devIdx = devMap[sw][nextHopNode];
                    uint32_t subnetIp = baseAddr + ((ci + 1) << 16); // 10.(ci+1).0.0
                    SwitchNode::AddMultipathRoute(sw, subnetIp, devIdx);
                    totalRoutes++;
                }
            }

            // (b) Downward exact routes: for each host in subtree
            for (uint32_t hostId : subtree[sw])
            {
                // Find the downward neighbor that leads to this host
                uint32_t nextDown = hostId; // default: direct connection
                if (t > 1)
                {
                    // Search neighbors at tier t-1 whose subtree contains hostId
                    for (uint32_t nb : adj[sw])
                    {
                        if (tier[nb] == t - 1 && subtree[nb].count(hostId))
                        {
                            nextDown = nb;
                            break;
                        }
                    }
                }

                int devIdx = devMap[sw][nextDown];
                for (uint32_t ci = 0; ci < numPaths; ci++)
                {
                    uint32_t ip = baseAddr + ((ci + 1) << 16) + (hostToIdx[hostId] + 1);
                    SwitchNode::AddMultipathRoute(sw, ip, devIdx);
                    totalRoutes++;
                }
            }
        }
    }

    NS_LOG_INFO("NDP auto multipath routes injected: total=" << totalRoutes
                << ", switches=" << (N - hostNodes.size())
                << ", subnet scheme=10.(pathIdx+1).0.0/16");

    return numPaths;
}

void
NdpMultipathHelper::SetBaseNetwork(Ipv4Address base, Ipv4Mask mask)
{
    m_baseNetwork = base;
    m_networkMask = mask;
}

void
NdpMultipathHelper::PrintConfiguration() const
{
    NS_LOG_INFO("===== NDP Multipath Configuration =====");
    NS_LOG_INFO("Number of paths: " << m_numPaths);
    NS_LOG_INFO("Base network: " << m_baseNetwork);
    NS_LOG_INFO("Network mask: " << m_networkMask);

    for (const auto& pair : m_hostPathIps)
    {
        uint32_t hostId = pair.first;
        const std::vector<Ipv4Address>& ips = pair.second;

        NS_LOG_INFO("Host " << hostId << " paths:");
        for (size_t i = 0; i < ips.size(); i++)
        {
            NS_LOG_INFO("  Path " << i << ": " << ips[i]);
        }
    }
    NS_LOG_INFO("=====================================");
}

} // namespace ns3
