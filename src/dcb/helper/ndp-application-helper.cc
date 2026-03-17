#include "ndp-application-helper.h"

#include "ns3/ndp-l4-protocol.h"
#include "ns3/dcb-net-device.h"

#include "ns3/boolean.h"
#include "ns3/data-rate.h"
#include "ns3/inet-socket-address.h"
#include "ns3/ipv4.h"
#include "ns3/log.h"
#include "ns3/names.h"
#include "ns3/node.h"
#include "ns3/string.h"
#include "ns3/uinteger.h"

#include <algorithm>
#include <boost/algorithm/string.hpp>
#include <fstream>
#include <random>
#include <sstream>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("NdpApplicationHelper");

NdpApplicationHelper::NdpApplicationHelper()
    : m_topology(nullptr)
{
    m_appFactory.SetTypeId("ns3::NdpTrafficGenApplication");
}

NdpApplicationHelper::~NdpApplicationHelper()
{
}

void
NdpApplicationHelper::SetTopology(Ptr<DcTopology> topology)
{
    m_topology = topology;
}


ApplicationContainer
NdpApplicationHelper::InstallNdpApplications(const boost::json::object& appConfig,
                                             Ptr<DcTopology> topology)
{
    NS_LOG_FUNCTION(this);
    m_topology = topology;

    ApplicationContainer apps;

    // Parse node specification
    std::string nodeSpec = "";
    if (appConfig.contains("nodes"))
    {
        nodeSpec = std::string(appConfig.at("nodes").as_string().c_str());
    }
    else
    {
        NS_FATAL_ERROR("Application config must specify 'nodes' field");
    }

    // Parse application configuration
    boost::json::object appCfg;
    if (appConfig.contains("applicationConfig"))
    {
        appCfg = appConfig.at("applicationConfig").as_object();
    }

    // Detect CDF mode
    bool isCdfMode = false;
    if (appCfg.contains("TrafficPattern"))
    {
        std::string patternStr = std::string(appCfg.at("TrafficPattern").as_string().c_str());
        isCdfMode = (patternStr == "CDF");
    }

    // For CDF mode with "all", only install on HOST nodes (not switches)
    std::vector<uint32_t> nodeIndices;
    if (isCdfMode && nodeSpec == "all")
    {
        for (auto it = topology->hosts_begin(); it != topology->hosts_end(); ++it)
        {
            nodeIndices.push_back(topology->GetNodeIndex(it->nodePtr));
        }
        NS_LOG_INFO("CDF mode: installing on " << nodeIndices.size() << " host nodes");
    }
    else
    {
        nodeIndices = ParseNodeSpec(nodeSpec, topology);
    }
    NS_LOG_INFO("Installing NDP applications on " << nodeIndices.size() << " nodes");

    // Parse timing
    Time startTime = Seconds(0.0);
    if (appConfig.contains("startTime"))
    {
        startTime = Time(std::string(appConfig.at("startTime").as_string().c_str()));
    }

    Time stopTime = Seconds(0.0);
    if (appConfig.contains("stopTime"))
    {
        stopTime = Time(std::string(appConfig.at("stopTime").as_string().c_str()));
    }

    // Install application on each node
    for (uint32_t nodeIdx : nodeIndices)
    {
        Ptr<Node> node = topology->GetNode(nodeIdx).nodePtr;

        // Ensure NDP L4 protocol is installed
        Ptr<NdpL4Protocol> ndpL4 = EnsureNdpL4Protocol(node);

        // Auto-inherit PULL pacing rate from the node's actual uplink
        DataRate hostLinkRate;
        {
            bool found = false;
            for (uint32_t d = 0; d < node->GetNDevices(); d++)
            {
                Ptr<DcbNetDevice> dev = DynamicCast<DcbNetDevice>(node->GetDevice(d));
                if (dev)
                {
                    hostLinkRate = dev->GetDataRate();
                    found = true;
                    break;
                }
            }
            if (found)
            {
                ndpL4->SetPullLinkRate(hostLinkRate);
                NS_LOG_INFO("Node " << nodeIdx
                            << ": PullLinkRate auto-set from DcbNetDevice → " << hostLinkRate);
            }
            else
            {
                NS_LOG_WARN("Node " << nodeIdx
                            << ": no DcbNetDevice found, PullLinkRate keeps default "
                            << ndpL4->GetPullLinkRate());
                hostLinkRate = ndpL4->GetPullLinkRate();
            }
        }

        // Create NDP socket and associate it with the L4 protocol
        Ptr<NdpSocket> socket = CreateObject<NdpSocket>();
        socket->SetNdp(ndpL4);

        // Set multipath IPs for the socket
        std::vector<Ipv4Address> pathIps = NdpMultipathHelper::GetInstance().GetPathIps(nodeIdx);
        if (pathIps.empty())
        {
            Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
            if (ipv4 && ipv4->GetNInterfaces() > 1)
            {
                pathIps.push_back(ipv4->GetAddress(1, 0).GetLocal());
            }
        }
        socket->SetPaths(pathIps);

        // Parse and set NDP socket configuration
        if (appConfig.contains("ndpConfig"))
        {
            const auto& ndpConfig = appConfig.at("ndpConfig").as_object();
            if (ndpConfig.contains("firstWindowSize"))
            {
                uint32_t firstWindowSize = static_cast<uint32_t>(ndpConfig.at("firstWindowSize").as_int64());
                socket->SetAttribute("FirstWindowSize", UintegerValue(firstWindowSize));
            }
            if (ndpConfig.contains("pullLinkRate"))
            {
                DataRate overrideRate(std::string(ndpConfig.at("pullLinkRate").as_string().c_str()));
                ndpL4->SetPullLinkRate(overrideRate);
                hostLinkRate = overrideRate;
            }
        }

        // Create application
        Ptr<NdpTrafficGenApplication> app = m_appFactory.Create<NdpTrafficGenApplication>();
        app->SetStartTime(startTime);
        if (stopTime.GetSeconds() > 0 && !isCdfMode)
        {
            app->SetStopTime(stopTime);
        }
        // CDF mode: do NOT set app stop time to the CDF generation window end.
        // The app must stay alive so in-flight flows can complete naturally.
        // ns-3 will stop the app when Simulator::Stop() fires.

        // Set the socket for the application
        app->SetSocket(socket);

        // Parse multipath configuration
        if (appConfig.contains("multipathConfig"))
        {
            ParseMultipathConfig(appConfig.at("multipathConfig").as_object(), app);
        }

        // Parse application-specific configuration
        bool sendEnabled = true;
        if (appCfg.contains("SendEnabled"))
        {
            sendEnabled = appCfg.at("SendEnabled").as_bool();
        }
        app->SetAttribute("SendEnabled", BooleanValue(sendEnabled));

        // Set traffic pattern and other sender parameters
        if (sendEnabled)
        {
            if (appCfg.contains("TrafficPattern"))
            {
                std::string patternStr = std::string(appCfg.at("TrafficPattern").as_string().c_str());
                app->SetTrafficPattern(GetTrafficPattern(patternStr));
            }

            if (isCdfMode)
            {
                // ── CDF mode: set CDF distribution, load, link rate, topology ──
                SetupCdfTraffic(appConfig, app, hostLinkRate, topology, nodeIdx, stopTime);
            }
            else
            {
                // ── Non-CDF modes ──
                if (appCfg.contains("FlowSize"))
                {
                    uint64_t flowSize = static_cast<uint64_t>(appCfg.at("FlowSize").as_int64());
                    app->SetFlowSize(flowSize);
                }

                if (appCfg.contains("DestFixed") && appCfg.at("DestFixed").as_bool())
                {
                    if (appCfg.contains("DestinationNode"))
                    {
                        uint32_t destNode = static_cast<uint32_t>(appCfg.at("DestinationNode").as_int64());
                        Ptr<Node> destNodePtr = topology->GetNode(destNode).nodePtr;
                        Ptr<Ipv4> destIpv4 = destNodePtr->GetObject<Ipv4>();
                        Ipv4Address destAddr = destIpv4->GetAddress(1, 0).GetLocal();
                        app->SetRemote(destAddr, 4000);

                        // Override socket paths with destination node's IPs
                        std::vector<Ipv4Address> destPathIps =
                            NdpMultipathHelper::GetInstance().GetPathIps(destNode);
                        if (destPathIps.empty())
                        {
                            destPathIps.push_back(destAddr);
                        }
                        socket->SetPaths(destPathIps);
                    }
                }
            }
        }

        node->AddApplication(app);
        apps.Add(app);

        NS_LOG_DEBUG("Installed NDP application on node " << nodeIdx);
    }

    return apps;
}

void
NdpApplicationHelper::ParseNdpConfig(const boost::json::object& ndpConfig,
                                     Ptr<NdpTrafficGenApplication> app)
{
    if (ndpConfig.contains("firstWindowSize"))
    {
        uint32_t firstWindowSize = static_cast<uint32_t>(ndpConfig.at("firstWindowSize").as_int64());
        // Set via socket attribute
        app->SetAttribute("FirstWindowSize", UintegerValue(firstWindowSize));
    }

    if (ndpConfig.contains("receiverLinkRate"))
    {
        std::string rateStr = std::string(ndpConfig.at("receiverLinkRate").as_string().c_str());
        DataRate rate(rateStr);
        // Set via socket attribute
        app->SetAttribute("ReceiverLinkRate", DataRateValue(rate));
    }
}

void
NdpApplicationHelper::ParseMultipathConfig(const boost::json::object& multipathConfig,
                                           Ptr<NdpTrafficGenApplication> app)
{
    if (multipathConfig.contains("numPaths"))
    {
        uint32_t numPaths = static_cast<uint32_t>(multipathConfig.at("numPaths").as_int64());
        // Path IPs will be set later after multipath helper configures routing
        NS_LOG_DEBUG("Multipath configured with " << numPaths << " paths");
    }

    if (multipathConfig.contains("pathSelection"))
    {
        std::string selection = std::string(multipathConfig.at("pathSelection").as_string().c_str());
        NS_LOG_DEBUG("Path selection: " << selection);
        // This is handled by NdpSocket internally
    }
}

Ptr<NdpL4Protocol>
NdpApplicationHelper::EnsureNdpL4Protocol(Ptr<Node> node)
{
    // Check if NDP L4 protocol is already installed
    Ptr<NdpL4Protocol> ndp = node->GetObject<NdpL4Protocol>();
    if (ndp == nullptr)
    {
        // Install NDP L4 protocol
        ndp = CreateObject<NdpL4Protocol>();
        node->AggregateObject(ndp);
        NS_LOG_DEBUG("Installed NDP L4 protocol on node " << node->GetId());
        
        // CRITICAL: Register NDP protocol with IPv4!
        Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
        if (ipv4)
        {
            ipv4->Insert(ndp);
            std::cout << "✅ Registered NDP protocol " << (int)NdpL4Protocol::PROT_NUMBER 
                      << " with IPv4 on node " << node->GetId() << std::endl;
            NS_LOG_INFO("Registered NDP L4 protocol (protocol " << (int)NdpL4Protocol::PROT_NUMBER 
                        << ") with IPv4 on node " << node->GetId());
        }
        else
        {
            std::cout << "❌ ERROR: IPv4 not found on node " << node->GetId() << std::endl;
            NS_LOG_WARN("Cannot register NDP protocol: IPv4 not found on node " << node->GetId());
        }
    }
    return ndp;
}

std::vector<uint32_t>
NdpApplicationHelper::ParseNodeSpec(const std::string& nodeSpec, Ptr<DcTopology> topology)
{
    std::vector<uint32_t> nodeIndices;

    if (nodeSpec == "all")
    {
        // All nodes
        for (uint32_t i = 0; i < topology->GetNNodes(); i++)
        {
            nodeIndices.push_back(i);
        }
    }
    else if (nodeSpec.find("[") != std::string::npos)
    {
        // Node specification with brackets.  Supported formats:
        //   "[N]"      – single node N
        //   "[A:B]"    – nodes A, A+1, …, B-1  (exclusive end, Python-style)
        //   "[A:]"     – nodes A … GetNNodes()-1
        //   "[:B]"     – nodes 0 … B-1
        std::string rangeStr = nodeSpec;
        rangeStr.erase(std::remove(rangeStr.begin(), rangeStr.end(), '['), rangeStr.end());
        rangeStr.erase(std::remove(rangeStr.begin(), rangeStr.end(), ']'), rangeStr.end());

        if (rangeStr.find(':') == std::string::npos)
        {
            // ── Single node: "[N]" ──────────────────────────────────────────
            uint32_t nodeId = static_cast<uint32_t>(std::stoi(rangeStr));
            NS_ABORT_MSG_UNLESS(nodeId < topology->GetNNodes(),
                                "ParseNodeSpec: node id " << nodeId
                                << " out of range (nNodes=" << topology->GetNNodes() << ")");
            nodeIndices.push_back(nodeId);
        }
        else
        {
            // ── Range: "[A:B]" / "[A:]" / "[:B]" ──────────────────────────
            std::vector<std::string> parts;
            boost::split(parts, rangeStr, boost::is_any_of(":"));

            uint32_t start = 0;
            uint32_t end = topology->GetNNodes();

            if (parts.size() >= 1 && !parts[0].empty())
            {
                start = static_cast<uint32_t>(std::stoi(parts[0]));
            }
            if (parts.size() >= 2 && !parts[1].empty())
            {
                end = static_cast<uint32_t>(std::stoi(parts[1]));
            }

            for (uint32_t i = start; i < end && i < topology->GetNNodes(); i++)
            {
                nodeIndices.push_back(i);
            }
        }
    }
    else if (nodeSpec == "random")
    {
        // Random subset - for now, just select half
        uint32_t count = topology->GetNNodes() / 2;
        std::vector<uint32_t> allNodes;
        for (uint32_t i = 0; i < topology->GetNNodes(); i++)
        {
            allNodes.push_back(i);
        }
        std::random_device rd;
        std::mt19937 g(rd());
        std::shuffle(allNodes.begin(), allNodes.end(), g);
        for (uint32_t i = 0; i < count; i++)
        {
            nodeIndices.push_back(allNodes[i]);
        }
    }
    else
    {
        // Single node or comma-separated list
        std::vector<std::string> parts;
        boost::split(parts, nodeSpec, boost::is_any_of(","));
        for (const auto& part : parts)
        {
            nodeIndices.push_back(std::stoi(part));
        }
    }

    return nodeIndices;
}

NdpTrafficGenApplication::TrafficPattern
NdpApplicationHelper::GetTrafficPattern(const std::string& patternStr)
{
    if (patternStr == "SEND_ONCE")
    {
        return NdpTrafficGenApplication::TrafficPattern::SEND_ONCE;
    }
    else if (patternStr == "CONTINUOUS")
    {
        return NdpTrafficGenApplication::TrafficPattern::CONTINUOUS;
    }
    else if (patternStr == "INCAST")
    {
        return NdpTrafficGenApplication::TrafficPattern::INCAST;
    }
    else if (patternStr == "BURST")
    {
        return NdpTrafficGenApplication::TrafficPattern::BURST;
    }
    else if (patternStr == "CDF")
    {
        return NdpTrafficGenApplication::TrafficPattern::CDF;
    }
    else
    {
        NS_FATAL_ERROR("Unknown traffic pattern: " << patternStr);
        return NdpTrafficGenApplication::TrafficPattern::SEND_ONCE;
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  CDF traffic pattern helpers
// ════════════════════════════════════════════════════════════════════════════

// static
std::unique_ptr<NdpTrafficGenApplication::TraceCdf>
NdpApplicationHelper::ConstructCdfFromFile(const std::string& filename)
{
    std::ifstream file(filename);
    NS_ASSERT_MSG(file.is_open(), "Cannot open CDF file: " << filename);

    auto cdf = std::make_unique<NdpTrafficGenApplication::TraceCdf>();
    std::string line;
    while (std::getline(file, line))
    {
        std::istringstream iss(line);
        uint32_t size;
        double prob;
        if (iss >> size >> prob)
        {
            cdf->push_back({size, prob});
        }
    }
    file.close();

    // Normalize: divide all probabilities by the maximum so the last entry is 1.0
    // (CDF files may use percentage format 0–100 instead of probability 0–1)
    if (!cdf->empty())
    {
        double maxProb = cdf->back().second;
        if (maxProb > 0 && maxProb != 1.0)
        {
            for (auto& [s, p] : *cdf)
            {
                p /= maxProb;
            }
        }
    }
    NS_LOG_INFO("Read CDF from " << filename << ": " << cdf->size() << " entries");
    return cdf;
}

void
NdpApplicationHelper::SetupCdfTraffic(const boost::json::object& appConfig,
                                      Ptr<NdpTrafficGenApplication> app,
                                      DataRate linkRate,
                                      Ptr<DcTopology> topology,
                                      uint32_t nodeIndex,
                                      Time stopTime)
{
    // 1. Parse CDF file
    NS_ASSERT_MSG(appConfig.contains("cdf"), "CDF traffic pattern requires 'cdf' config block");
    const auto& cdfObj = appConfig.at("cdf").as_object();
    std::string cdfFile = std::string(cdfObj.at("cdfFile").as_string().c_str());

    auto cdf = ConstructCdfFromFile(cdfFile);

    // Optional: scale by avgSize or scaleFactor
    if (cdfObj.contains("avgSize"))
    {
        uint32_t avgSize = 0;
        if (cdfObj.at("avgSize").is_int64())
            avgSize = static_cast<uint32_t>(cdfObj.at("avgSize").as_int64());
        else if (cdfObj.at("avgSize").is_uint64())
            avgSize = static_cast<uint32_t>(cdfObj.at("avgSize").as_uint64());

        if (avgSize > 0)
        {
            // Compute current mean
            double mean = 0.0;
            auto [ls, lp] = (*cdf)[0];
            for (const auto& [sz, prob] : *cdf)
            {
                mean += (sz + ls) / 2.0 * (prob - lp);
                ls = sz;
                lp = prob;
            }
            double sf = static_cast<double>(avgSize) / mean;
            for (auto& [sz, prob] : *cdf)
            {
                sz = static_cast<uint32_t>(sz * sf);
            }
        }
    }
    if (cdfObj.contains("scaleFactor"))
    {
        double sf = 0.0;
        if (cdfObj.at("scaleFactor").is_double())
            sf = cdfObj.at("scaleFactor").as_double();
        else if (cdfObj.at("scaleFactor").is_int64())
            sf = static_cast<double>(cdfObj.at("scaleFactor").as_int64());

        if (sf > 0.0)
        {
            for (auto& [sz, prob] : *cdf)
            {
                sz = static_cast<uint32_t>(sz * sf);
            }
        }
    }

    app->SetFlowCdf(*cdf);

    // 2. Parse traffic load
    double load = 0.5;
    if (appConfig.contains("applicationConfig"))
    {
        const auto& ac = appConfig.at("applicationConfig").as_object();
        if (ac.contains("TrafficLoad"))
        {
            if (ac.at("TrafficLoad").is_double())
                load = ac.at("TrafficLoad").as_double();
            else if (ac.at("TrafficLoad").is_int64())
                load = static_cast<double>(ac.at("TrafficLoad").as_int64());
        }
    }
    app->SetTrafficLoad(load);

    // 3. Set link rate, topology info, and stop time
    app->SetLinkRate(linkRate);
    app->SetTopologyInfo(topology, nodeIndex);
    app->SetCdfStopTime(stopTime);

    NS_LOG_INFO("CDF setup for node " << nodeIndex
                << ": load=" << load << " linkRate=" << linkRate
                << " stopTime=" << stopTime.GetSeconds() << "s");
}

void
NdpApplicationHelper::ConfigureMultipathRouting(const boost::json::object& multipathConfig,
                                                NodeContainer hosts,
                                                NodeContainer switches,
                                                Ptr<DcTopology> topology)
{
    uint32_t numPaths = 4; // Default
    if (multipathConfig.contains("numPaths"))
    {
        numPaths = static_cast<uint32_t>(multipathConfig.at("numPaths").as_int64());
    }

    // Single path → no multipath routing needed.
    // CRITICAL: Do NOT call AssignMultipathIps() here!
    // If we assign multipath IPs (e.g. 10.1.0.X) but skip route injection,
    // the switch won't have routes for these IPs → all packets fall back to
    // default-route → 0 flows complete, massive RTOs.
    // By returning without assigning, GetPathIps() returns empty and
    // InstallNdpApplications() falls back to the node's primary IP (10.0.0.X)
    // which is already in the switch's GlobalRouter routing table.
    if (numPaths <= 1)
    {
        NS_LOG_INFO("numPaths=" << numPaths << ", skipping multipath routing and IP assignment.");
        return;
    }

    std::string baseNetwork = "10.0.0.0";
    if (multipathConfig.contains("baseNetwork"))
    {
        baseNetwork = std::string(multipathConfig.at("baseNetwork").as_string().c_str());
    }

    std::string networkMask = "255.255.0.0";
    if (multipathConfig.contains("networkMask"))
    {
        networkMask = std::string(multipathConfig.at("networkMask").as_string().c_str());
    }

    // Determine topology type
    std::string topoType = "auto"; // default: auto-detect any topology
    if (multipathConfig.contains("topology"))
    {
        topoType = std::string(multipathConfig.at("topology").as_string().c_str());
    }

    // ── Phase 1: Auto-detect topology and configure switch routing ──────
    // ConfigureAutoRouting returns the actual number of viable paths.
    // If the topology can't support multipath (e.g. star with 1 switch),
    // actualPaths will be ≤ 1 and we fall back to single-path mode.
    uint32_t actualPaths = numPaths;

    if ((topoType == "auto" || topoType == "fattree") && topology != nullptr)
    {
        // Pre-set baseNetwork so ConfigureAutoRouting uses the correct address
        NdpMultipathHelper::GetInstance().SetBaseNetwork(
            Ipv4Address(baseNetwork.c_str()), Ipv4Mask(networkMask.c_str()));
        actualPaths = NdpMultipathHelper::GetInstance().ConfigureAutoRouting(topology, numPaths);
        if (actualPaths <= 1)
        {
            NS_LOG_INFO("Topology supports only " << actualPaths
                        << " path(s) — falling back to single-path mode.");
            actualPaths = 1;
        }
    }
    else if (topoType == "leafspine" && switches.GetN() > 0)
    {
        // Legacy explicit leaf-spine routing (requires manually specifying switch roles)
        NodeContainer spines;
        uint32_t numSpines = std::min(numPaths, switches.GetN());
        for (uint32_t i = 0; i < numSpines; i++)
        {
            spines.Add(switches.Get(switches.GetN() - numSpines + i));
        }
        NodeContainer leafs;
        for (uint32_t i = 0; i < switches.GetN() - numSpines; i++)
        {
            leafs.Add(switches.Get(i));
        }
        NdpMultipathHelper::GetInstance().ConfigureLeafSpineRouting(hosts, leafs, spines);
    }
    else
    {
        NS_LOG_WARN("Cannot configure multipath routing: topology type '"
                    << topoType << "' with topology=" << (topology ? "present" : "null"));
        actualPaths = 1;
    }

    // ── Phase 2: Assign multipath IPs based on actual path count ────────
    // This MUST happen after ConfigureAutoRouting so we know the real path count.
    if (actualPaths >= 2)
    {
        // True multipath: assign one IP per path per host
        NdpMultipathHelper::GetInstance().AssignMultipathIps(
            hosts, actualPaths, baseNetwork.c_str(), networkMask.c_str());
        NS_LOG_INFO("Configured multipath routing (" << topoType
                    << ") with " << actualPaths << " paths (requested " << numPaths << ")");
    }
    else
    {
        // Single-path topology (e.g. star): no multipath IPs needed.
        // NdpSocket will use the node's primary IP (fallback in InstallNdpApplications).
        NS_LOG_INFO("Single-path mode — no multipath IPs assigned "
                    "(topology: " << topoType << ")");
    }
}

} // namespace ns3
