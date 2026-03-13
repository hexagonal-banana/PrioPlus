/*
 * Copyright (c) 2023
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
 *
 * Author: Zhaochen Zhang (zhaochen.zhang@outlook.com)
 */
 #include "json-outputs.h"

 #include "ns3/dcb-traffic-gen-application.h"
 #include "ns3/fifo-queue-disc-ecn.h"
 #include "ns3/json-utils.h"
 #include "ns3/ndp-switch-queue.h"
 #include "ns3/ndp-traffic-gen-application.h"
 #include "ns3/rocev2-dcqcn.h"
 #include "ns3/rocev2-hpcc.h"
 #include "ns3/rocev2-prioplus-swift.h"
 #include "ns3/rocev2-swift.h"
 #include "ns3/rocev2-timely.h"
 
 #include <fstream>
 #include <iostream>
 #include <map>
 #include <numeric>
 #include <time.h>
 
 namespace ns3
 {
 
 NS_LOG_COMPONENT_DEFINE("JsonUtil");
 
 namespace json_util
 {
 
 std::ofstream
 CreateOutputFile(boost::json::object& conf, std::string confFileName)
 {
     /**
      * If the output file name has *, we will replace it with the config file name.
      * For example, if the output file is output/\*-test and the config file name is
      * config/subfolder/config.json, the output file will be output/subfolder/config-test.json
      * Note that use this feature need to ensure the config file is in config/ folder.
      */
     std::string outputFile =
         conf["outputFile"].get_object().find("resultFile")->value().as_string().c_str();
     // Check if the output file name has *
     if (outputFile.find("*") != std::string::npos)
     {
         // Replace the * with the config file name with the content between "config/" and ".json"
         /**
          * If the confFileName start with a slash, it is a absolute path, we use the path behind
          * "config/". To resolve this case, we assume the config is in the "config/" folder.
          */
         std::string confFileNameWithoutPathAndSuffix;
         if (confFileName.find("config/") != std::string::npos)
         {
             confFileNameWithoutPathAndSuffix =
                 confFileName.substr(confFileName.find("config/") + 7);
             confFileNameWithoutPathAndSuffix = confFileNameWithoutPathAndSuffix.substr(
                 0,
                 confFileNameWithoutPathAndSuffix.find(".json"));
         }
         else
         {
             NS_FATAL_ERROR(
                 "The config file name does not contain 'config/' when the output wildcard is used");
         }
         outputFile.replace(outputFile.find("*"), 1, confFileNameWithoutPathAndSuffix);
     }
 
     // If the folder of the output file does not exist, create it
     std::string outputFolder = outputFile.substr(0, outputFile.find_last_of('/'));
     if (!outputFolder.empty())
     {
         std::string command = "mkdir -p " + outputFolder;
         // Use the return value to avoid the warning of unused return value
         int ret = system(command.c_str());
         // Check if the folder is created successfully
         if (ret != 0)
         {
             NS_FATAL_ERROR("Cannot create folder " << outputFolder);
         }
     }
 
     std::ofstream ofs(outputFile);
     if (!ofs.is_open())
     {
         NS_FATAL_ERROR("Cannot open file " << outputFile);
     }
 
     return ofs;
 }
 
 void
 OutputStats(boost::json::object& conf,
             ApplicationContainer& apps,
             Ptr<DcTopology> topology,
             std::string confFileName)
 {
     std::ofstream ofs = CreateOutputFile(conf, confFileName);
 
     boost::json::object outputObj;
     outputObj["config"] = conf;
     std::shared_ptr<boost::json::object> appStatsObj = ConstructAppStatsObj(apps);
     outputObj["overallStatistics"] = appStatsObj->find("overallStatistics")->value();
 
     // Construct flow statistics
     FlowStatsObjMap mFlowStatsObjs;
     ConstructSenderFlowStats(apps, mFlowStatsObjs);
     ConstructRealTimeFlowStats(apps, mFlowStatsObjs);
     boost::json::array flowStatsArray;
     uint32_t flowId = 0;
     for (auto kvPair : mFlowStatsObjs)
     {
         // Replace the flow id at the beginning of the object
         kvPair.second->find("flowId")->value() = flowId++;
         flowStatsArray.emplace_back(*kvPair.second);
         // std::cout << "Flow " << flowId << " statistics" << std::endl;
     }
     outputObj["flowStatistics"] = flowStatsArray;
 
     // Construct switch statistics
     boost::json::object switchStatsObj;
     Time startTime =
         Time(outputObj["overallStatistics"].as_object().find("startTimeNs")->value().as_int64());
     Time finishTime =
         Time(outputObj["overallStatistics"].as_object().find("finishTimeNs")->value().as_int64());
     ConstructSwitchStats(topology, switchStatsObj, startTime, finishTime);
     outputObj["switchStatistics"] = switchStatsObj.find("switchStats")->value();
 
     // Add NDP-specific statistics if enabled
     if (conf.contains("globalConfig"))
     {
         const auto& globalConfig = conf.at("globalConfig").as_object();
         
         // Collect NDP switch queue statistics
         if (globalConfig.contains("ndpTrimStats") &&
             globalConfig.at("ndpTrimStats").as_bool())
         {
             boost::json::object ndpStatsObj;
             uint64_t totalTrims = 0;
             uint64_t totalRts = 0;
             
             for (uint32_t i = 0; i < topology->GetNNodes(); i++)
             {
                 Ptr<Node> node = topology->GetNode(i).nodePtr;
                 // Iterate through all devices on the node
                 for (uint32_t j = 0; j < node->GetNDevices(); j++)
                 {
                     Ptr<NetDevice> device = node->GetDevice(j);
                     Ptr<TrafficControlLayer> tc = node->GetObject<TrafficControlLayer>();
                     if (tc)
                     {
                         Ptr<QueueDisc> qd = tc->GetRootQueueDiscOnDevice(device);
                         Ptr<NdpSwitchQueue> ndpQueue = DynamicCast<NdpSwitchQueue>(qd);
                         if (ndpQueue)
                         {
                             totalTrims += ndpQueue->GetTrimCount();
                             totalRts += ndpQueue->GetReturnToSenderCount();
                         }
                     }
                 }
             }
             
             ndpStatsObj["totalTrimCount"] = totalTrims;
             ndpStatsObj["totalReturnToSenderCount"] = totalRts;
             outputObj["ndpStatistics"] = ndpStatsObj;
             
             NS_LOG_INFO("NDP Statistics: Trims=" << totalTrims << ", RTS=" << totalRts);
         }
     }
 
     // Write the output object to file
     PrettyPrint(ofs, outputObj);
 }
 
 std::shared_ptr<boost::json::object>
ConstructAppStatsObj(ApplicationContainer& apps)
{
    std::shared_ptr<boost::json::object> appStatsObj = std::make_shared<boost::json::object>();
    boost::json::object overallStatsObj;

    // Variables used to calculate overall statistics
    // The start time of the first flow and the end time of the last flow
    Time startTime = Time::Max();  // Fixed: Initialize to max, not current time
    Time finishTime = Time(0);
    // Total bytes of all flows, used to calculate the total throughput rate
    uint32_t totalPkts = 0;
    uint64_t totalBytes = 0;
    uint32_t totalSentPkts = 0;
    uint64_t totalSentBytes = 0;
    uint32_t retxCount = 0;
    // FCT of all flows, used to calculate the average and percentile FCT
    std::vector<Time> vFct;

   for (uint32_t i = 0; i < apps.GetN(); ++i)
   {
       // Try DcbBaseApplication first (RoCEv2/TCP)
       Ptr<DcbBaseApplication> app = DynamicCast<DcbBaseApplication>(apps.Get(i));
       if (app != nullptr)
       {
           auto appStats = app->GetStats();

           // Get variables of the overall statistics
           startTime = std::min(startTime, appStats->tStart);
           finishTime = std::max(finishTime, appStats->tFinish);
           totalPkts += appStats->nTotalSizePkts;
           totalBytes += appStats->nTotalSizeBytes;

           if (app->GetProtoGroup() == DcbTrafficGenApplication::ProtocolGroup::RoCEv2)
           {
               totalSentPkts += appStats->nTotalSentPkts;
               totalSentBytes += appStats->nTotalSentBytes;
               retxCount += appStats->nRetxCount;
           }
           else if (app->GetProtoGroup() == DcbTrafficGenApplication::ProtocolGroup::TCP)
           {
               // TCP does not support totalSent and retxCount, o we use the totalSizePkts and
               // totalSizeBytes to calculate the retx rate, which will be zero.
               totalSentPkts += appStats->nTotalSizePkts;
               totalSentBytes += appStats->nTotalSizeBytes;
           }

           // Per flow statistics
           auto mFlowStats = appStats->mFlowStats;
           for (const auto& pFlowStats : mFlowStats)
           {
               vFct.push_back(pFlowStats.second->tFct);
           }
       }
       else
       {
           // Try NdpTrafficGenApplication
            Ptr<NdpTrafficGenApplication> ndpApp = DynamicCast<NdpTrafficGenApplication>(apps.Get(i));
           if (ndpApp != nullptr && ndpApp->IsSendEnabled())
           {
               // Main (non-CDF) socket stats
               auto ndpStats = ndpApp->GetNdpFlowStats();
               if (ndpStats->tFct > Time(0))
               {
                   vFct.push_back(ndpStats->tFct);
                   totalBytes += ndpStats->nTotalSizeBytes;
                   totalSentBytes += ndpStats->nTotalSentBytes;
                   totalPkts += ndpStats->nTotalSizePkts;
                   totalSentPkts += ndpStats->nTotalSentPkts;
                   retxCount += ndpStats->nRetxCount;

                   if (ndpStats->tStart.IsStrictlyPositive())
                   {
                       startTime = std::min(startTime, ndpStats->tStart);
                   }
                   if (ndpStats->tFinish.IsStrictlyPositive())
                   {
                       finishTime = std::max(finishTime, ndpStats->tFinish);
                   }
               }

               // CDF completed flow stats
               const auto& cdfStats = ndpApp->GetCdfCompletedStats();
               for (const auto& cdfFlowStats : cdfStats)
               {
                   if (cdfFlowStats->tFct > Time(0))
                       vFct.push_back(cdfFlowStats->tFct);
                   totalBytes += cdfFlowStats->nTotalSizeBytes;
                   totalSentBytes += cdfFlowStats->nTotalSentBytes;
                   totalPkts += cdfFlowStats->nTotalSizePkts;
                   totalSentPkts += cdfFlowStats->nTotalSentPkts;
                   retxCount += cdfFlowStats->nRetxCount;

                   if (cdfFlowStats->tStart.IsStrictlyPositive())
                       startTime = std::min(startTime, cdfFlowStats->tStart);
                   if (cdfFlowStats->tFinish.IsStrictlyPositive())
                       finishTime = std::max(finishTime, cdfFlowStats->tFinish);
               }
           }
       }
   }
 
     // Calculate overall statistics
     double duration = (finishTime - startTime).GetSeconds();
     overallStatsObj["totalThroughputBitps"] = 
         (duration > 0) ? (totalBytes * 8.0 / duration) : 0.0;
     overallStatsObj["totalRetxRatePkt"] = 
         (totalPkts > 0) ? (double(totalSentPkts - totalPkts) / totalPkts) : 0.0;
     overallStatsObj["totalRetxRateByte"] = 
         (totalBytes > 0) ? (double(totalSentBytes - totalBytes) / totalBytes) : 0.0;
     overallStatsObj["startTimeNs"] = startTime.GetNanoSeconds();
     overallStatsObj["finishTimeNs"] = finishTime.GetNanoSeconds();
     
     // Calculate average and percentile FCT
     std::sort(vFct.begin(), vFct.end());
     uint32_t nFct = vFct.size();
     if (nFct > 0)
     {
         // Average FCT is calculated by the total FCT / number of flows
         Time avgFct = std::accumulate(vFct.begin(), vFct.end(), Time(0)) / nFct;
         Time p95Fct = vFct[std::min((uint32_t)(0.95 * nFct), nFct - 1)];
         Time p99Fct = vFct[std::min((uint32_t)(0.99 * nFct), nFct - 1)];
         Time p999Fct = vFct[std::min((uint32_t)(0.999 * nFct), nFct - 1)];
         overallStatsObj["avgFctNs"] = avgFct.GetNanoSeconds();
         overallStatsObj["p95FctNs"] = p95Fct.GetNanoSeconds();
         overallStatsObj["p99FctNs"] = p99Fct.GetNanoSeconds();
         overallStatsObj["p999FctNs"] = p999Fct.GetNanoSeconds();
     }
     else
     {
         overallStatsObj["avgFctNs"] = 0;
         overallStatsObj["p95FctNs"] = 0;
         overallStatsObj["p99FctNs"] = 0;
         overallStatsObj["p999FctNs"] = 0;
     }
 
     appStatsObj->emplace("overallStatistics", overallStatsObj);
     return appStatsObj;
 }
 
void
ConstructSenderFlowStats(ApplicationContainer& apps, FlowStatsObjMap& mFlowStatsObjs)
{
    uint32_t ndpFlowCounter = 0; // Counter for NDP flows
    
    for (uint32_t i = 0; i < apps.GetN(); ++i)
    {
        // Try DcbBaseApplication first
        Ptr<DcbBaseApplication> app = DynamicCast<DcbBaseApplication>(apps.Get(i));
        if (app == nullptr)
        {
            // Try NdpTrafficGenApplication
            Ptr<NdpTrafficGenApplication> ndpApp = DynamicCast<NdpTrafficGenApplication>(apps.Get(i));
            if (ndpApp != nullptr && ndpApp->IsSendEnabled())
            {
                // Helper lambda to emit one NDP flow entry
                auto emitNdpFlow = [&](std::shared_ptr<NdpSocket::Stats> stats,
                                       Ptr<NdpSocket> socket,
                                       const std::string& flowType) {
                    FlowIdentifier flowId;
                    Address localAddr, peerAddr;

                    if (socket != nullptr)
                    {
                        socket->GetSockName(localAddr);
                        socket->GetPeerName(peerAddr);

                        if (InetSocketAddress::IsMatchingType(localAddr) &&
                            InetSocketAddress::IsMatchingType(peerAddr))
                        {
                            InetSocketAddress local = InetSocketAddress::ConvertFrom(localAddr);
                            InetSocketAddress peer = InetSocketAddress::ConvertFrom(peerAddr);
                            flowId.srcAddr = local.GetIpv4();
                            flowId.srcPort = local.GetPort();
                            flowId.dstAddr = peer.GetIpv4();
                            flowId.dstPort = peer.GetPort();
                        }
                        else
                        {
                            flowId.srcAddr = Ipv4Address();
                            flowId.srcPort = ndpFlowCounter;
                            flowId.dstAddr = Ipv4Address();
                            flowId.dstPort = 0;
                        }
                    }
                    else
                    {
                        flowId.srcAddr = Ipv4Address();
                        flowId.srcPort = ndpFlowCounter;
                        flowId.dstAddr = Ipv4Address();
                        flowId.dstPort = 0;
                    }

                    auto flowStatsObj = std::make_shared<boost::json::object>();
                    mFlowStatsObjs[flowId] = flowStatsObj;

                    (*flowStatsObj)["flowId"] = ndpFlowCounter;
                    (*flowStatsObj)["flowIdentifier"] = boost::json::object{
                        {"srcAddr", flowId.GetSrcAddrString()},
                        {"srcPort", (int64_t)flowId.srcPort},
                        {"dstAddr", flowId.GetDstAddrString()},
                        {"dstPort", (int64_t)flowId.dstPort}};
                    (*flowStatsObj)["flowType"] = flowType;
                    (*flowStatsObj)["flowTag"] = stats->flowTag.empty() ? "ndp-flow" : stats->flowTag;
                    (*flowStatsObj)["totalSizePkts"] = (int64_t)stats->nTotalSizePkts;
                    (*flowStatsObj)["totalSizeBytes"] = (int64_t)stats->nTotalSizeBytes;
                    (*flowStatsObj)["retxCount"] = (int64_t)stats->nRetxCount;
                    (*flowStatsObj)["fctNs"] = stats->tFct.GetNanoSeconds();
                    (*flowStatsObj)["startNs"] = stats->tStart.GetNanoSeconds();
                    (*flowStatsObj)["finishNs"] = stats->tFinish.GetNanoSeconds();
                    (*flowStatsObj)["overallFlowRate"] = stats->overallFlowRate.GetBitRate();

                    boost::json::object ccStatsObj;
                    ccStatsObj["totalAcks"] = (int64_t)stats->acksReceived;
                    ccStatsObj["totalNacks"] = (int64_t)stats->nacksReceived;
                    ccStatsObj["totalPullsConsumed"] = (int64_t)stats->pullsConsumed;
                    ccStatsObj["totalRtoFires"] = (int64_t)stats->rtoFires;
                    ccStatsObj["totalRtoTrueLoss"] = (int64_t)stats->rtoTrueLoss;

                    if (stats->bDetailedSenderStats)
                    {
                        boost::json::array sentPktArray;
                        for (const auto& [time, size] : stats->vSentPkt)
                        {
                            sentPktArray.emplace_back(
                                boost::json::object{{"timeNs", time.GetNanoSeconds()},
                                                    {"sizeByte", (int64_t)size}});
                        }
                        (*flowStatsObj)["sentPkt"] = sentPktArray;
                        (*flowStatsObj)["ccRate"] = boost::json::array{};
                        (*flowStatsObj)["ccCwnd"] = boost::json::array{};
                        (*flowStatsObj)["recvEcn"] = boost::json::array{};
                    }

                    if (stats->bDetailedRetxStats)
                    {
                        boost::json::array recvAckArray;
                        for (const auto& [time, seq] : stats->vRecvAck)
                        {
                            recvAckArray.emplace_back(
                                boost::json::object{{"timeNs", time.GetNanoSeconds()},
                                                    {"seq", (int64_t)seq}});
                        }
                        ccStatsObj["recvAck"] = recvAckArray;

                        boost::json::array recvNackArray;
                        for (const auto& [time, seq] : stats->vRecvNack)
                        {
                            recvNackArray.emplace_back(
                                boost::json::object{{"timeNs", time.GetNanoSeconds()},
                                                    {"seq", (int64_t)seq}});
                        }
                        ccStatsObj["recvNack"] = recvNackArray;
                    }

                    (*flowStatsObj)["ccStats"] = ccStatsObj;
                    ndpFlowCounter++;
                };

                // Main (non-CDF) flow
                auto ndpStats = ndpApp->GetNdpFlowStats();
                if (ndpStats->tFct > Time(0))
                {
                    emitNdpFlow(ndpStats, ndpApp->GetSocket(), "NDP");
                }

                // CDF completed flows
                const auto& cdfStats = ndpApp->GetCdfCompletedStats();
                for (const auto& cdfFlowStats : cdfStats)
                {
                    emitNdpFlow(cdfFlowStats, nullptr, "NDP-CDF");
                }
            }
            continue;
        }
        auto appStats = app->GetStats();
 
         Ptr<DcbTrafficGenApplication> tgApp = DynamicCast<DcbTrafficGenApplication>(apps.Get(i));
         std::shared_ptr<DcbTrafficGenApplication::Stats> tgAppStats;
         if (tgApp != nullptr)
         {
             // convert to DcbTrafficGenApplication::Stats
             tgAppStats =
                 std::dynamic_pointer_cast<DcbTrafficGenApplication::Stats>(tgApp->GetStats());
         }
 
         // Per flow statistics
         auto mFlowStats = appStats->mFlowStats;
         for (auto pFlowStats : mFlowStats)
         {
             FlowIdentifier flowIdentifier = pFlowStats.first;
             auto flowStats = pFlowStats.second;
             // In this function, we will create a new json object for each flow
             // and store it in the map
             auto flowStatsObj = std::make_shared<boost::json::object>();
             // mFlowStatsObjs.insert(std::make_pair(flowIdentifier, flowStatsObj));
             mFlowStatsObjs[flowIdentifier] = flowStatsObj;
 
             (*flowStatsObj)["flowId"] = 0; // This will be filled later
             (*flowStatsObj)["flowIdentifier"] =
                 boost::json::object{{"srcAddr", flowIdentifier.GetSrcAddrString()},
                                     {"srcPort", flowIdentifier.srcPort},
                                     {"dstAddr", flowIdentifier.GetDstAddrString()},
                                     {"dstPort", flowIdentifier.dstPort}};
             (*flowStatsObj)["flowType"] = appStats->appFlowType;
             if (tgAppStats != nullptr)
             {
                 (*flowStatsObj)["flowTag"] = tgAppStats->mFlowStats[flowIdentifier]->flowTag;
             }
             (*flowStatsObj)["totalSizePkts"] = flowStats->nTotalSizePkts;
             (*flowStatsObj)["totalSizeBytes"] = flowStats->nTotalSizeBytes;
             if (app->GetProtoGroup() == DcbTrafficGenApplication::ProtocolGroup::RoCEv2)
             {
                 (*flowStatsObj)["retxCount"] = flowStats->nRetxCount;
             }
             (*flowStatsObj)["fctNs"] = flowStats->tFct.GetNanoSeconds();
             (*flowStatsObj)["startNs"] = flowStats->tStart.GetNanoSeconds();
             (*flowStatsObj)["finishNs"] = flowStats->tFinish.GetNanoSeconds();
             (*flowStatsObj)["overallFlowRate"] = flowStats->overallFlowRate.GetBitRate();
             (*flowStatsObj)["flowTag"] = flowStats->flowTag;
 
             // Detailed statistics
             if (flowStats->bDetailedSenderStats)
             {
                 boost::json::array ccRateArray;
                 boost::json::array ccCwndArray;
                 boost::json::array recvEcnArray;
                 boost::json::array sentPktArray;
 
                 for (auto ccRate : flowStats->vCcRate)
                 {
                     ccRateArray.emplace_back(
                         boost::json::object{{"timeNs", ccRate.first.GetNanoSeconds()},
                                             {"rateBitps", ccRate.second.GetBitRate()}});
                 }
                 for (auto ccCwnd : flowStats->vCcCwnd)
                 {
                     ccCwndArray.emplace_back(
                         boost::json::object{{"timeNs", ccCwnd.first.GetNanoSeconds()},
                                             {"cwndByte", ccCwnd.second}});
                 }
                 for (auto recvEcn : flowStats->vRecvEcn)
                 {
                     recvEcnArray.emplace_back(
                         boost::json::object{{"timeNs", recvEcn.GetNanoSeconds()}});
                 }
                 for (auto sentPkt : flowStats->vSentPkt)
                 {
                     sentPktArray.emplace_back(
                         boost::json::object{{"timeNs", sentPkt.first.GetNanoSeconds()},
                                             {"sizeByte", sentPkt.second}});
                 }
 
                 (*flowStatsObj)["ccRate"] = ccRateArray;
                 (*flowStatsObj)["ccCwnd"] = ccCwndArray;
                 (*flowStatsObj)["recvEcn"] = recvEcnArray;
                 (*flowStatsObj)["sentPkt"] = sentPktArray;
 
                 // If the ccOps has ccStats, we will add the ccStats to the flowStatsObj
                 if (flowStats->ccStats != nullptr)
                 {
                     boost::json::object ccStatsObj;
 
                     boost::json::array rateArray;
 
                     for (auto& [time, rate] : flowStats->ccStats->vCcRate)
                     {
                         rateArray.emplace_back(
                             boost::json::object{{"timeNs", time.GetNanoSeconds()},
                                                 {"rateBitps", rate.GetBitRate()}});
                     }
 
                     ccStatsObj["rate"] = rateArray;
 
                     std::shared_ptr<RoCEv2Hpcc::Stats> hpccCcStats =
                         std::dynamic_pointer_cast<RoCEv2Hpcc::Stats>(flowStats->ccStats);
                     std::shared_ptr<RoCEv2Swift::Stats> swiftCcStats =
                         std::dynamic_pointer_cast<RoCEv2Swift::Stats>(flowStats->ccStats);
                     std::shared_ptr<RoCEv2Timely::Stats> timelyCcStats =
                         std::dynamic_pointer_cast<RoCEv2Timely::Stats>(flowStats->ccStats);
                     std::shared_ptr<RoCEv2PrioplusSwift::Stats> prioplusSwiftCcStats =
                         std::dynamic_pointer_cast<RoCEv2PrioplusSwift::Stats>(flowStats->ccStats);
                     if (hpccCcStats != nullptr)
                     {
                         boost::json::array delayArray;
                         boost::json::array uArray;
 
                         for (auto& [sendTime, recvTime, delay] : hpccCcStats->vPacketDelay)
                         {
                             delayArray.emplace_back(
                                 boost::json::object{{"sendTimeNs", sendTime.GetNanoSeconds()},
                                                     {"recvTimeNs", recvTime.GetNanoSeconds()},
                                                     {"delayNs", delay.GetNanoSeconds()}});
                         }
 
                         for (auto& [time, u] : hpccCcStats->vU)
                         {
                             uArray.emplace_back(
                                 boost::json::object{{"timeNs", time.GetNanoSeconds()}, {"u", u}});
                         }
 
                         ccStatsObj["delay"] = delayArray;
                         ccStatsObj["u"] = uArray;
                     }
                     else if (prioplusSwiftCcStats != nullptr)
                     {
                         boost::json::array completeStatsArray;
 
                         for (RoCEv2PrioplusSwift::Stats::PrioplusSwiftCompleteStats& completeStats :
                              prioplusSwiftCcStats->vPrioplusCompleteStats)
                         {
                             completeStatsArray.emplace_back(boost::json::object{
                                 {"timeNs", completeStats.tNow.GetNanoSeconds()},
                                 {"delayNs", completeStats.tDelay.GetNanoSeconds()},
                                 {"cwnd", completeStats.cwnd},
                                 {"incastAvoidanceRate", completeStats.dIncastAvoidance},
                                 {"aiPart", completeStats.aiPart},
                                 {"miPart", completeStats.miPart},
                                 {"mdPart", completeStats.mdPart}});
                         }
 
                         ccStatsObj["completeStats"] = completeStatsArray;
                     }
                     else if (hpccCcStats != nullptr)
                     {
                         boost::json::array delayArray;
                         boost::json::array uArray;
 
                         for (auto& [sendTime, recvTime, delay] : hpccCcStats->vPacketDelay)
                         {
                             delayArray.emplace_back(
                                 boost::json::object{{"sendTimeNs", sendTime.GetNanoSeconds()},
                                                     {"recvTimeNs", recvTime.GetNanoSeconds()},
                                                     {"delayNs", delay.GetNanoSeconds()}});
                         }
 
                         for (auto& [time, u] : hpccCcStats->vU)
                         {
                             uArray.emplace_back(
                                 boost::json::object{{"timeNs", time.GetNanoSeconds()}, {"u", u}});
                         }
 
                         ccStatsObj["delay"] = delayArray;
                         ccStatsObj["u"] = uArray;
                     }
                     else if (swiftCcStats != nullptr)
                     {
                         boost::json::array ccRateChangeArray;
                         boost::json::array targetDelayArray;
 
                         for (auto& [time, ccRateChange] : swiftCcStats->vCcRateChange)
                         {
                             ccRateChangeArray.emplace_back(boost::json::object{
                                 {"timeNs", time.GetNanoSeconds()},
                                 {"ccRateChange", ccRateChange ? "increase" : "decrease"}});
                         }
                         for (auto& [time, targetDelay] : swiftCcStats->vTargetDelay)
                         {
                             targetDelayArray.emplace_back(boost::json::object{
                                 {"timeNs", time.GetNanoSeconds()},
                                 {"targetDelayNs", targetDelay.GetNanoSeconds()}});
                         }
 
                         ccStatsObj["ccRateChange"] = ccRateChangeArray;
                         ccStatsObj["targetDelay"] = targetDelayArray;
                     }
                     else if (timelyCcStats != nullptr)
                     {
                         boost::json::array delayArray;
                         boost::json::array delayGradientArray;
 
                         for (auto& [sendTime, recvTime, delay] : timelyCcStats->vPacketDelay)
                         {
                             delayArray.emplace_back(
                                 boost::json::object{{"sendTimeNs", sendTime.GetNanoSeconds()},
                                                     {"recvTimeNs", recvTime.GetNanoSeconds()},
                                                     {"delayNs", delay.GetNanoSeconds()}});
                         }
 
                         for (auto& [sendTime, recvTime, delayGradient] :
                              timelyCcStats->vPacketDelayGradient)
                         {
                             delayGradientArray.emplace_back(
                                 boost::json::object{{"sendTimeNs", sendTime.GetNanoSeconds()},
                                                     {"recvTimeNs", recvTime.GetNanoSeconds()},
                                                     {"delayGradient", delayGradient}});
                         }
 
                         ccStatsObj["delay"] = delayArray;
                         ccStatsObj["delayGradient"] = delayGradientArray;
                     }
 
                     (*flowStatsObj)["ccStats"] = ccStatsObj;
                 }
             }
 
             if (flowStats->bDetailedRetxStats)
             {
                 // Add the sent psn into the sentPktArray if bDetailedSenderStats is true
                 // Otherwise, we will create a new array
                 if (flowStats->bDetailedSenderStats)
                 {
                     boost::json::array& sentPktArray = (*flowStatsObj)["sentPkt"].as_array();
                     for (uint32_t i = 0; i < flowStats->vSentPsn.size(); ++i)
                     {
                         // The sentPktArray is already created, so we just need to add the
                         // psn
                         sentPktArray[i].as_object().emplace("psn", flowStats->vSentPsn[i].second);
                     }
                 }
                 else
                 {
                     boost::json::array sentPktArray;
                     for (auto sentPsn : flowStats->vSentPsn)
                     {
                         sentPktArray.emplace_back(
                             boost::json::object{{"timeNs", sentPsn.first.GetNanoSeconds()},
                                                 {"psn", sentPsn.second}});
                     }
                     (*flowStatsObj)["sentPkt"] = sentPktArray;
                 }
 
                 boost::json::array recvAckArray;
                 // vExpectedPsn must has stats, but vAckedPsn may not
                 bool bHasAckedPsn = flowStats->vAckedPsn.size() > 0;
                 // vAckedPsn's number of elements must be leq than vExpectedPsn's as a ack
                 // may carry acked psn, but must carry expected psn. Thus we use a index to
                 // iterate vAckedPsn.
                 uint32_t ackedPsnIndex = 0;
                 for (auto expectedPsn : flowStats->vExpectedPsn)
                 {
                     boost::json::object recvAckObj;
                     recvAckObj.emplace("timeNs", expectedPsn.first.GetNanoSeconds());
                     recvAckObj.emplace("expectedPsn", expectedPsn.second);
                     if (bHasAckedPsn &&
                         flowStats->vAckedPsn[ackedPsnIndex].first == expectedPsn.first)
                     {
                         recvAckObj.emplace("ackedPsn",
                                            flowStats->vAckedPsn[ackedPsnIndex++].second);
                     }
                     recvAckArray.emplace_back(recvAckObj);
                 }
                 (*flowStatsObj)["recvAck"] = recvAckArray;
             }
         }
     }
 }
 
 void
 ConstructRealTimeFlowStats(ApplicationContainer& apps, FlowStatsObjMap& mFlowStatsObjs)
 {
     for (uint32_t i = 0; i < apps.GetN(); ++i)
     {
         Ptr<RealTimeApplication> app = DynamicCast<RealTimeApplication>(apps.Get(i));
         if (app == nullptr)
             continue;
         auto appStats = app->GetStats();
         std::shared_ptr<RealTimeApplication::Stats> rtaStats =
             std::dynamic_pointer_cast<RealTimeApplication::Stats>(appStats);
         if (rtaStats == nullptr)
         {
             NS_FATAL_ERROR("Cannot cast to RealTimeApplication::Stats");
         }
 
         for (auto pFlowStats : rtaStats->mflowStats)
         {
             FlowIdentifier flowIdentifier = pFlowStats.first;
             auto flowStats = pFlowStats.second;
             // In this function, every flow should already have a json object
             // We just need to add the real time stats to the object
             auto flowStatsObj = mFlowStatsObjs[flowIdentifier];
 
             (*flowStatsObj)["throughputFromArriveBitps"] =
                 flowStats->rAvgRateFromArrive.GetBitRate();
             (*flowStatsObj)["throughputFromRecvBitps"] = flowStats->rAvgRateFromRecv.GetBitRate();
 
             // Calculate average and percentile delay
             std::vector<Time> vArriveDelay = flowStats->vArriveDelay;
             std::vector<Time> vTxDelay = flowStats->vTxDelay;
             std::sort(vArriveDelay.begin(), vArriveDelay.end());
             std::sort(vTxDelay.begin(), vTxDelay.end());
             uint32_t nArriveDelay = vArriveDelay.size();
             uint32_t nTxDelay = vTxDelay.size();
             Time avgArriveDelay =
                 std::accumulate(vArriveDelay.begin(), vArriveDelay.end(), Time(0)) / nArriveDelay;
             Time avgTxDelay = std::accumulate(vTxDelay.begin(), vTxDelay.end(), Time(0)) / nTxDelay;
             Time p95ArriveDelay = vArriveDelay[0.95 * nArriveDelay];
             Time p99ArriveDelay = vArriveDelay[0.99 * nArriveDelay];
             Time p95TxDelay = vTxDelay[0.95 * nTxDelay];
             Time p99TxDelay = vTxDelay[0.99 * nTxDelay];
             (*flowStatsObj)["avgArriveDelayNs"] = avgArriveDelay.GetNanoSeconds();
             (*flowStatsObj)["p95ArriveDelayNs"] = p95ArriveDelay.GetNanoSeconds();
             (*flowStatsObj)["p99ArriveDelayNs"] = p99ArriveDelay.GetNanoSeconds();
             (*flowStatsObj)["avgTxDelayNs"] = avgTxDelay.GetNanoSeconds();
             (*flowStatsObj)["p95TxDelayNs"] = p95TxDelay.GetNanoSeconds();
             (*flowStatsObj)["p99TxDelayNs"] = p99TxDelay.GetNanoSeconds();
 
             // Add detailed stats
             if (rtaStats->bDetailedSenderStats)
             {
                 boost::json::array recvPktArray;
                 for (auto recvPkt : flowStats->vRecvPkt)
                 {
                     recvPktArray.emplace_back(
                         boost::json::object{{"timeNs", recvPkt.first.GetNanoSeconds()},
                                             {"sizeByte", recvPkt.second}});
                 }
                 (*flowStatsObj)["recvPkt"] = recvPktArray;
             }
         }
     }
 }
 
 void
 ConstructSwitchStats(Ptr<DcTopology> topology,
                      boost::json::object& switchStatsObj,
                      Time startTime,
                      Time finishTime)
 {
     boost::json::array switchStatsArray;
     for (auto switchIter = topology->switches_begin(); switchIter != topology->switches_end();
          switchIter++)
     {
         boost::json::object localSwitchStatsObj;
         boost::json::array localSwitchStatsArray;
         localSwitchStatsObj.emplace("switchId", topology->GetNodeIndex(switchIter->nodePtr));
         const uint32_t ndev = switchIter->nodePtr->GetNDevices();
         // Iterate all the devices of the switch
         for (uint32_t devi = 0; devi < ndev; devi++)
         {
             Ptr<DcbNetDevice> dev = DynamicCast<DcbNetDevice>(switchIter->nodePtr->GetDevice(devi));
             if (dev == nullptr)
             {
                 continue;
             }
 
            // Get the PausableQueueDisc of the device
            Ptr<PausableQueueDisc> pqd = dev->GetQueueDisc();
            if (pqd == nullptr)
            {
                // Try to get NdpSwitchQueue through TrafficControlLayer
                Ptr<TrafficControlLayer> tc = switchIter->nodePtr->GetObject<TrafficControlLayer>();
                if (tc != nullptr)
                {
                    Ptr<QueueDisc> qdisc = tc->GetRootQueueDiscOnDevice(dev);
                    if (qdisc != nullptr)
                    {
                        Ptr<NdpSwitchQueue> ndpQueue = DynamicCast<NdpSwitchQueue>(qdisc);
                        if (ndpQueue != nullptr)
                        {
                            boost::json::object portStatsObj;
                            boost::json::array portStatsArray;
                            portStatsObj.emplace("portId", devi);

                            // Helper lambda to build one queue's JSON object
                            auto buildQueueJson = [&](const NdpSwitchQueue::QueueStats& qs,
                                                      int queueId,
                                                      const std::string& label) {
                                boost::json::object qObj;
                                qObj.emplace("queueId", queueId);
                                qObj.emplace("maxQLengthPackets", (int64_t)qs.maxQLengthPackets);
                                qObj.emplace("maxQLengthBytes", (int64_t)qs.maxQLengthBytes);

                                if (queueId == 0) // NDP-specific counters on low queue
                                {
                                    qObj.emplace("trimCount",
                                                 (int64_t)ndpQueue->GetTrimCount());
                                    qObj.emplace("returnToSenderCount",
                                                 (int64_t)ndpQueue->GetReturnToSenderCount());
                                }

                                // Percentiles (pad with zeros for idle intervals, same as RoCEv2)
                                std::vector<uint32_t> vQLenBytes;
                                for (const auto& [t, b] : qs.vQLengthBytes)
                                    vQLenBytes.push_back(b);

                                if (!qs.detailedQlength && !vQLenBytes.empty())
                                {
                                    StringValue sv;
                                    Time recordInterval = Time(0);
                                    if (GlobalValue::GetValueByNameFailSafe(
                                            "qlengthRecordInterval", sv))
                                        recordInterval = Time(sv.Get());
                                    if (recordInterval.IsStrictlyPositive() &&
                                        finishTime > startTime)
                                    {
                                        uint32_t nPoints =
                                            (double)(finishTime - startTime).GetNanoSeconds() /
                                            (double)recordInterval.GetNanoSeconds();
                                        nPoints = std::min(nPoints, (uint32_t)10000000);
                                        while (vQLenBytes.size() < nPoints)
                                            vQLenBytes.push_back(0);
                                    }
                                }

                                if (!vQLenBytes.empty())
                                {
                                    std::sort(vQLenBytes.begin(), vQLenBytes.end());
                                    uint32_t n = vQLenBytes.size();
                                    uint64_t sum = 0;
                                    for (auto v : vQLenBytes) sum += v;
                                    qObj.emplace("avgQLengthBytes", (int64_t)(sum / n));
                                    qObj.emplace("p25QLengthBytes",
                                        (int64_t)vQLenBytes[std::min((uint32_t)(0.25*n), n-1)]);
                                    qObj.emplace("p50QLengthBytes",
                                        (int64_t)vQLenBytes[std::min((uint32_t)(0.50*n), n-1)]);
                                    qObj.emplace("p75QLengthBytes",
                                        (int64_t)vQLenBytes[std::min((uint32_t)(0.75*n), n-1)]);
                                    qObj.emplace("p95QLengthBytes",
                                        (int64_t)vQLenBytes[std::min((uint32_t)(0.95*n), n-1)]);
                                    qObj.emplace("p99QLengthBytes",
                                        (int64_t)vQLenBytes[std::min((uint32_t)(0.99*n), n-1)]);
                                }

                                // Time-series (only when detailed or interval-based produced data)
                                boost::json::array qLengthArray;
                                for (const auto& [time, bytes] : qs.vQLengthBytes)
                                {
                                    qLengthArray.emplace_back(
                                        boost::json::object{
                                            {"timeNs", time.GetNanoSeconds()},
                                            {"lengthBytes", (int64_t)bytes}});
                                }
                                qObj.emplace("qLength", qLengthArray);

                                // ecnInfo / pfcTime: NDP doesn't use these, keep empty
                                qObj.emplace("ecnInfo", boost::json::array{});
                                qObj.emplace("pfcTime", boost::json::array{});

                                return qObj;
                            };

                            // Queue 0: LowQueue (data)
                            portStatsArray.emplace_back(
                                buildQueueJson(ndpQueue->GetLowQueueStats(), 0, "low"));
                            // Queue 1: HighQueue (control)
                            portStatsArray.emplace_back(
                                buildQueueJson(ndpQueue->GetHighQueueStats(), 1, "high"));

                            portStatsObj.emplace("queueStats", portStatsArray);
                            localSwitchStatsArray.emplace_back(portStatsObj);
                            continue;
                        }
                    }
                }
                
                // Skip if neither PausableQueueDisc nor NdpSwitchQueue
                // NS_LOG_WARN("Device " << devi << " does not have PausableQueueDisc or NdpSwitchQueue, skipping stats");
                continue;
            }
             std::shared_ptr<PausableQueueDisc::Stats> pStats = pqd->GetStats();
             boost::json::object portStatsObj;
             boost::json::array portStatsArray;
             portStatsObj.emplace("portId", devi);
             // Iterate all the queues of the device
             uint8_t qIdx = 0;
             for (std::shared_ptr<FifoQueueDiscEcn::Stats> qStats : pStats->vQueueStats)
             {
                 boost::json::object queueStatsObj;
                 queueStatsObj.emplace("queueId", qIdx++);
                 queueStatsObj.emplace("maxQLengthPackets", qStats->nMaxQLengthPackets);
                 queueStatsObj.emplace("maxQLengthBytes", qStats->nMaxQLengthBytes);
 
                 // Calculate average and percentile queue length
                 // The calculate is only precise when detailedQlengthStats is false
                 std::vector<uint32_t> vQLengthBytes;
                 for (auto qLength : qStats->vQLengthBytes)
                 {
                     vQLengthBytes.push_back(qLength.second);
                 }
                if (!qStats->bDetailedQlengthStats)
                {
                    StringValue sv;
                    Time recordInterval = Time(0);
                    if (GlobalValue::GetValueByNameFailSafe("qlengthRecordInterval", sv))
                        recordInterval = Time(sv.Get());
                    if (recordInterval.IsStrictlyPositive() && finishTime > startTime)
                    {
                        uint32_t nPoints =
                            ((double)finishTime.GetNanoSeconds() - (double)startTime.GetNanoSeconds()) /
                            (double)recordInterval.GetNanoSeconds();
                        nPoints = std::min(nPoints, (uint32_t)10000000);
                        while (vQLengthBytes.size() < nPoints)
                        {
                            vQLengthBytes.push_back(0);
                        }
                    }
                }
                 if (vQLengthBytes.size() != 0)
                 {
                     std::sort(vQLengthBytes.begin(), vQLengthBytes.end());
                     uint32_t nQLengthBytes = vQLengthBytes.size();
                     uint32_t avgQLengthBytes =
                         std::accumulate(vQLengthBytes.begin(), vQLengthBytes.end(), 0) /
                         nQLengthBytes;
                     uint32_t p25QLengthBytes = vQLengthBytes[0.25 * nQLengthBytes];
                     uint32_t p50QLengthBytes = vQLengthBytes[0.50 * nQLengthBytes];
                     uint32_t p75QLengthBytes = vQLengthBytes[0.75 * nQLengthBytes];
                     uint32_t p95QLengthBytes = vQLengthBytes[0.95 * nQLengthBytes];
                     uint32_t p99QLengthBytes = vQLengthBytes[0.99 * nQLengthBytes];
                     queueStatsObj.emplace("avgQLengthBytes", avgQLengthBytes);
                     queueStatsObj.emplace("p25QLengthBytes", p25QLengthBytes);
                     queueStatsObj.emplace("p50QLengthBytes", p50QLengthBytes);
                     queueStatsObj.emplace("p75QLengthBytes", p75QLengthBytes);
                     queueStatsObj.emplace("p95QLengthBytes", p95QLengthBytes);
                     queueStatsObj.emplace("p99QLengthBytes", p99QLengthBytes);
                 }
 
                 if (qStats->bDetailedQlengthStats)
                 {
                     // Detailed stats
                     boost::json::array qLengthArray;
                     for (auto qLength : qStats->vQLengthBytes)
                     {
                         qLengthArray.emplace_back(
                             boost::json::object{{"timeNs", qLength.first.GetNanoSeconds()},
                                                 {"lengthBytes", qLength.second}});
                     }
                     queueStatsObj.emplace("qLength", qLengthArray);
 
                     // If background congestion control type is set, add the
                     // backgroundCongestion
                     if (qStats->backgroundCongestionTypeId != TypeId())
                     {
                         boost::json::array bgQlengthArray;
                         for (auto bgQlength : qStats->vBackgroundQLengthBytes)
                         {
                             bgQlengthArray.emplace_back(
                                 boost::json::object{{"timeNs", bgQlength.first.GetNanoSeconds()},
                                                     {"lengthBytes", bgQlength.second}});
                         }
                         queueStatsObj.emplace("backgroundQlength", bgQlengthArray);
                     }
 
                     boost::json::array ecnTimeArray;
                     for (auto& [ecnTime, flowIdentifier, psn, size] : qStats->vEcn)
                     {
                         ecnTimeArray.emplace_back(
                             boost::json::object{{"timeNs", ecnTime.GetNanoSeconds()},
                                                 {"srcAddr", flowIdentifier.GetSrcAddrString()},
                                                 {"srcPort", flowIdentifier.srcPort},
                                                 {"dstAddr", flowIdentifier.GetDstAddrString()},
                                                 {"dstPort", flowIdentifier.dstPort},
                                                 {"psn", psn},
                                                 {"sizeByte", size}});
                     }
                     queueStatsObj.emplace("ecnInfo", ecnTimeArray);
 
                     // Add a pfcTimeArray to the queueStatsObj, which will be filled later
                     boost::json::array pfcTimeArray;
                     queueStatsObj.emplace("pfcTime", pfcTimeArray);
                 }
 
                 if (qStats->bDetailedDeviceThroughputStats)
                 {
                     boost::json::array deviceThroughputArray;
                     for (auto& [time, throughput] : qStats->vDeviceThroughput)
                     {
                         deviceThroughputArray.emplace_back(
                             boost::json::object{{"timeNs", time.GetNanoSeconds()},
                                                 {"throughputBitps", throughput.GetBitRate()}});
                     }
                     queueStatsObj.emplace("deviceThroughput", deviceThroughputArray);
                 }
 
                 // Add the queueStatsObj to the portStatsArray
                 portStatsArray.emplace_back(queueStatsObj);
             }
             // After iterating all the queues, add the pause/resume status to each queue
             for (const auto& [time, prio, pr] : pStats->vPauseResumeTime)
             {
                 // The time is the time when the pause/resume happens
                 // The prio is the priority of the pause/resume
                 // The pr is the pause/resume status
                 boost::json::object& queueStatsObj = portStatsArray[prio].as_object();
                 boost::json::array& pfcTimeArray = queueStatsObj["pfcTime"].as_array();
                 pfcTimeArray.emplace_back(
                     boost::json::object{{"timeNs", time.GetNanoSeconds()},
                                         {"pfcType", pr ? "pause" : "resume"}});
             }
 
             portStatsObj.emplace("queueStats", portStatsArray);
             // Add the portStatsObj to the localSwitchStatsObj
             localSwitchStatsArray.emplace_back(portStatsObj);
         }
 
         localSwitchStatsObj.emplace("portStats", localSwitchStatsArray);
         // Add the switch stats to the switchStatsArray, use the node id as the key
         switchStatsArray.emplace_back(localSwitchStatsObj);
     }
     switchStatsObj.emplace("switchStats", switchStatsArray);
 }
 
 void
 DisableDetailedSwitchStats(boost::json::object& configObj, Ptr<DcTopology> topology)
 {
     // Disable the detailed switch stats for some switches if is set in
     // configObj["runtimeConfig"]["disabledDetailedSwitchStats"]
     if (configObj["runtimeConfig"].get_object().find("disabledDetailedSwitchStats") !=
         configObj["runtimeConfig"].get_object().end())
     {
         std::string disabledSwitches = configObj["runtimeConfig"]
                                            .get_object()
                                            .find("disabledDetailedSwitchStats")
                                            ->value()
                                            .as_string()
                                            .c_str();
         std::vector<uint32_t> vDisabledSwitch =
             json_util::ConvertRangeToVector(disabledSwitches, topology->GetNNodes());
         // Iterate over all switches
         for (auto swIt = topology->switches_begin(); swIt != topology->switches_end(); swIt++)
         {
             DcTopology::TopoNode& sw = *swIt;
             // If the switch is in the disabled list, disable the detailed stats
             if (std::find(vDisabledSwitch.begin(), vDisabledSwitch.end(), sw->GetId()) !=
                 vDisabledSwitch.end())
             {
                 // Iterate over all ports of the switch
                 for (uint32_t devI = 0; devI < sw->GetNDevices(); devI++)
                 {
                     Ptr<DcbNetDevice> dev = DynamicCast<DcbNetDevice>(sw->GetDevice(devI));
                     if (dev == nullptr)
                     {
                         continue;
                     }
                     Ptr<PausableQueueDisc> qdisc =
                         DynamicCast<PausableQueueDisc>(dev->GetQueueDisc());
                     if (qdisc == nullptr)
                     {
                         NS_FATAL_ERROR("QueueDisc is not PausableQueueDisc");
                     }
                     qdisc->SetDetailedSwitchStats(false);
                 }
             }
         }
     }
 }
 
 } // namespace json_util
 
 } // namespace ns3
 