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
#include "json-application-helper.h"

#include "ns3/fifo-queue-disc-ecn.h"
#include "ns3/json-utils.h"
#include "ns3/rng-stream.h"
#include "ns3/rocev2-hpcc.h"
#include "ns3/rocev2-swift.h"
#include "ns3/rocev2-timely.h"

#include <boost/algorithm/string.hpp>
#include <fstream>
#include <iostream>
#include <map>
#include <numeric>
#include <time.h>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("JsonApplicationHelper");

namespace json_util
{

/***** Utilities about application *****/
typedef std::function<ApplicationContainer(const boost::json::object&, Ptr<DcTopology>)>
    AppInstallFunc;
static ApplicationContainer InstallDcbTrafficGenApplication(const boost::json::object& appConfig,
                                                            Ptr<DcTopology> topology);

static std::map<std::string, AppInstallFunc> appInstallMapper = {
    {"DcbTrafficGenApplication", InstallDcbTrafficGenApplication},
    // {"PacketSink", ProtobufTopologyLoader::InstallPacketSink},
    // {"PreGeneratedApplication", ProtobufTopologyLoader::InstallPreGeneratedApplication}
};

static std::map<std::string, DcbTrafficGenApplication::ProtocolGroup> protocolGroupMapper = {
    {"RAW_UDP", DcbTrafficGenApplication::ProtocolGroup::RAW_UDP},
    {"TCP", DcbTrafficGenApplication::ProtocolGroup::TCP},
    {"RoCEv2", DcbTrafficGenApplication::ProtocolGroup::RoCEv2},
};

ApplicationContainer
InstallApplications(const boost::json::object& conf, Ptr<DcTopology> topology)
{
    boost::json::array appConfigs =
        JsonGetArrayOrRaise(conf, "applicationConfig", "Cannot find applicationConfig field");
    ApplicationContainer apps;
    // This fucntion can be extended to install more sets of applications using this for loop
    for (const auto& appConfig : appConfigs)
    {
        apps.Add(InstallDcbTrafficGenApplication(appConfig.as_object(), topology));
    }
    return apps;
}

/***** Utilities about DcbTrafficGenApplication *****/

std::shared_ptr<DcbTrafficGenApplicationHelper>
ConstructTraceAppHelper(const boost::json::object& appConfig, Ptr<DcTopology> topology)
{
    std::shared_ptr<DcbTrafficGenApplicationHelper> appHelper =
        std::make_shared<DcbTrafficGenApplicationHelper>(topology);

    // Application type
    std::string appType = JsonGetStringOrRaise(appConfig, "appType", "Need to specify \"appType\"");
    appHelper->SetApplicationTypeId(TypeId::LookupByName(appType));

    // Set protocol group
    std::string sProtocolGroup =
        JsonGetStringOrRaise(appConfig,
                             "protocolGroup",
                             "Using DcbTrafficGenApplication needs to specify \"protocolGroup\"");
    auto pProtocolGroup = protocolGroupMapper.find(sProtocolGroup);
    if (pProtocolGroup == protocolGroupMapper.end())
    {
        NS_FATAL_ERROR("Cannot recognize protocol group \"" << sProtocolGroup << "\"");
    }
    appHelper->SetProtocolGroup(pProtocolGroup->second);

    // Set CDF
    auto cdfObjIt = appConfig.find("cdf");
    if (cdfObjIt != appConfig.end())
    {
        if (cdfObjIt->value().is_object())
        {
            // No CDF type specified, use CDF Read from file
            boost::json::object cdfConfig = appConfig.find("cdf")->value().as_object();
            auto cdf = ConstructCdf(cdfConfig);
            appHelper->SetCdf(std::move(cdf));
        }
        else
        {
            NS_FATAL_ERROR("Cannot recognize CDF type");
        }
    }

    if (appConfig.find("applicationConfig") != appConfig.end())
    {
        boost::json::object localAppConfig =
            appConfig.find("applicationConfig")->value().as_object();
        std::unique_ptr<std::vector<DcbTrafficGenApplicationHelper::ConfigEntry_t>>
            appConfigVector = ConstructConfigVector(localAppConfig);
        appHelper->SetAppAttributes(std::move(*appConfigVector));
    }

    if (appConfig.find("socketConfig") != appConfig.end())
    {
        boost::json::object socketConfigObj = appConfig.find("socketConfig")->value().as_object();
        std::unique_ptr<std::vector<DcbTrafficGenApplicationHelper::ConfigEntry_t>>
            socketConfigVector = ConstructConfigVector(socketConfigObj);
        appHelper->SetSocketAttributes(std::move(*socketConfigVector));
    }

    // Set cc config
    if (appConfig.find("congestionConfig") != appConfig.end())
    {
        boost::json::object ccConfig = appConfig.find("congestionConfig")->value().as_object();
        std::unique_ptr<std::vector<DcbTrafficGenApplicationHelper::ConfigEntry_t>> ccConfigVector =
            ConstructConfigVector(ccConfig);
        appHelper->SetCcAttributes(std::move(*ccConfigVector));
    }

    // Get the start time of the application
    Time startTime =
        Time(JsonGetStringOrRaise(appConfig,
                                  "startTime",
                                  "Using DcbTrafficGenApplication needs to specify \"startTime\""));

    // Get the stop time of the application, if not specified, set to 0
    Time stopTime = Time(0);
    if (appConfig.find("stopTime") != appConfig.end())
    {
        stopTime = Time(appConfig.find("stopTime")->value().as_string().c_str());
    }
    appHelper->SetStartAndStopTime(startTime, stopTime);

    return appHelper;
}

static ApplicationContainer
InstallDcbTrafficGenApplication(const boost::json::object& appConfig, Ptr<DcTopology> topology)
{
    std::shared_ptr<DcbTrafficGenApplicationHelper> appHelper =
        ConstructTraceAppHelper(appConfig, topology);
    ApplicationContainer apps;

    // Install the application on nodes specified in the config
    // TODO We just assume that the application is installed on all the hosts
    if (appConfig.find("nodes") == appConfig.end())
    {
        NS_FATAL_ERROR("Using DcbTrafficGenApplication needs to specify \"load\"");
    }
    boost::json::string nodes = appConfig.find("nodes")->value().get_string().c_str();
    if (nodes == "all")
    {
        // TODO so ugly, need to be refactored
        // if has groupCongestionConfig, set the attributes accordingly
        // if nodes is "all", each node will be installed with multiple applications
        if (appConfig.find("groupCongestionConfig") != appConfig.end())
        {
            uint32_t nApp = 1;
            boost::json::object gConf =
                appConfig.find("groupCongestionConfig")->value().as_object();
            JsonCallIfExistsInt<uint32_t>(gConf, "applicationNumber", [&nApp](int n) { nApp = n; });
            for (uint32_t i = 0; i < nApp; ++i)
            {
                SetGroupAttributes(gConf, appHelper, nApp, i);
                for (auto hostIter = topology->hosts_begin(); hostIter != topology->hosts_end();
                     hostIter++)
                {
                    Ptr<Node> node = hostIter->nodePtr;
                    apps.Add(appHelper->Install(node));
                }
            }
        }
        else
        {
            for (auto hostIter = topology->hosts_begin(); hostIter != topology->hosts_end();
                 hostIter++)
            {
                if (hostIter->type != DcTopology::TopoNode::NodeType::HOST)
                {
                    NS_FATAL_ERROR("Node "
                                   << topology->GetNodeIndex(hostIter->nodePtr)
                                   << " is not a host and thus could not install an application.");
                }
                Ptr<Node> node = hostIter->nodePtr;
                apps.Add(appHelper->Install(node));
            }
        }
    }
    // if nodes start with "random [num]"
    else if (boost::algorithm::starts_with(nodes.c_str(), "random"))
    {
        // If the nodes are specified, convert the string to vector
        // Here we assume the nodes are specified in the format like "random [num]".
        std::string numStr = nodes.c_str();
        numStr = numStr.substr(nodes.find_first_of(" ") + 1);
        uint32_t num = std::stoi(numStr);
        std::set<uint32_t> vHosts = GetRandomHosts(num, topology->GetNHosts());
        // vHosts are index of topology's hosts, not the index of nodes
        for (auto hostIter = topology->hosts_begin(); hostIter != topology->hosts_end(); hostIter++)
        {
            if (hostIter->type != DcTopology::TopoNode::NodeType::HOST)
            {
                NS_FATAL_ERROR("Node "
                               << topology->GetNodeIndex(hostIter->nodePtr)
                               << " is not a host and thus could not install an application.");
            }
            if (vHosts.find(topology->GetNodeIndex(hostIter->nodePtr)) != vHosts.end())
            {
                Ptr<Node> node = hostIter->nodePtr;
                apps.Add(appHelper->Install(node));
            }
        }
    }
    else
    {
        // If the nodes are specified, convert the string to vector
        // Here we assume the nodes are specified in the format like "[1:3,5,7:]".
        std::vector<uint32_t> vNodes = ConvertRangeToVector(nodes.c_str(), topology->GetNHosts());

        // Get the start time and the flow interval
        Time startTime = Time(appConfig.find("startTime")->value().as_string().c_str());
        Time flowInterval = Time(0);
        // If the flow interval is not set, this will have no effect
        if (appConfig.find("flowInterval") != appConfig.end())
        {
            flowInterval = Time(appConfig.find("flowInterval")->value().as_string().c_str());
        }
        // If the intervalGroupSize is set, the interval between the groups will be added with a
        // interval
        uint32_t intervalGroupSize = 1;
        JsonCallIfExistsInt<uint32_t>(appConfig,
                                      "intervalGroupSize",
                                      [&intervalGroupSize](int size) { intervalGroupSize = size; });
        Time duration = Time(0);
        // If the duration is not set, this will have no effect
        if (appConfig.find("duration") != appConfig.end())
        {
            duration = Time(appConfig.find("duration")->value().as_string().c_str());
        }
        // If the randomRange is set, the start time will be added with a unified random value in
        // the range, if not set, the random value will be 0
        Time randomRange = Time(0);
        JsonCallIfExistsString(appConfig, "randomRange", [&randomRange](const std::string& str) {
            randomRange = Time(str);
        });
        Ptr<UniformRandomVariable> rand = CreateObject<UniformRandomVariable>();
        rand->SetAttribute("Min", DoubleValue(0));
        rand->SetAttribute("Max", DoubleValue(randomRange.GetNanoSeconds()));

        for (uint32_t i = 0; i < vNodes.size(); ++i)
        {
            Ptr<Node> node = topology->GetNode(vNodes[i]).nodePtr;

            Time flowStartTime = startTime + (i / intervalGroupSize) * flowInterval +
                                 Time::FromDouble(rand->GetValue(), Time::NS);
            if (duration != Time(0))
            {
                appHelper->SetStartAndStopTime(flowStartTime, flowStartTime + duration);
            }
            else
            {
                appHelper->SetStartTime(flowStartTime);
            }

            // if has groupCongestionConfig, set the attributes according to the index
            if (appConfig.find("groupCongestionConfig") != appConfig.end())
            {
                boost::json::object gConf =
                    appConfig.find("groupCongestionConfig")->value().as_object();
                SetGroupAttributes(gConf, appHelper, vNodes.size(), i);
            }

            apps.Add(appHelper->Install(node));
        }
    }

    return apps;
}

void
SetGroupAttributes(const boost::json::object& gConf,
                   std::shared_ptr<DcbTrafficGenApplicationHelper> helper,
                   uint32_t groupSize,
                   uint32_t idx)
{
    // Get the per group size, default is 1
    uint32_t gSize = 1;
    JsonCallIfExistsInt<uint32_t>(gConf, "groupSize", [&gSize](int size) { gSize = size; });
    groupSize /= gSize;
    // Iterate over the attributes in the group
    for (auto kvPair : gConf)
    {
        std::string name(kvPair.key());
        if (name == "groupSize" || name == "applicationNumber")
        {
            continue;
        }
        // Check if name is in format of "Congestion::xxx"
        // Split the name into two parts
        std::vector<std::string> vName;
        // boost::split(vName, name, boost::is_any_of("::"));
        // This split will split "Congestion::xxx" into "Congestion", "" and "xxx"
        // So we need to use the following split
        boost::split(vName, name, boost::is_any_of("::"), boost::token_compress_on);
        if (vName.size() != 2)
        {
            NS_FATAL_ERROR("Attribute name \"" << name << "\" is not in format of \"xxx::xxx\"");
        }
        std::string className = vName[0];
        std::string attributeName = vName[1];
        Ptr<AttributeValue> attrValue;

        // If the value of the attribute is a object, it will have two fields: "Begin" and "End"
        if (kvPair.value().is_object())
        {
            // Calculate the value of the attribute according to the index
            boost::json::object valueObj = kvPair.value().as_object();
            boost::json::value beginValue = valueObj.find("begin")->value();
            boost::json::value endValue = valueObj.find("end")->value();

            // The value can be int or string, check if it is a string
            bool isString = beginValue.is_string();

            if (isString)
            {
                // Now assume the value is a sting with number and unit with space, like "50 KB"
                std::string beginStr = beginValue.as_string().c_str();
                std::string endStr = endValue.as_string().c_str();
                // Convert the string into two parts, number and unit
                std::vector<std::string> vBegin;
                boost::split(vBegin, beginStr, boost::is_any_of(" "), boost::token_compress_on);
                std::vector<std::string> vEnd;
                boost::split(vEnd, endStr, boost::is_any_of(" "), boost::token_compress_on);
                // If have unit check the unit is the same
                NS_ASSERT_MSG((vBegin.size() == 1 && vEnd.size() == 1) ||
                                  (vBegin.size() == 2 && vEnd.size() == 2 && vBegin[1] == vEnd[1]),
                              "The unit of begin and end should be the same");

                bool isFloat = false;
                if (valueObj.find("isFloat") != valueObj.end())
                {
                    isFloat = valueObj.find("isFloat")->value().get_bool();
                }

                if (isFloat)
                {
                    double begin = std::stod(vBegin[0]);
                    double end = std::stod(vEnd[0]);
                    double value =
                        begin + (idx / gSize) * (double)(end - begin) / (double)(groupSize - 1);
                    if (vBegin.size() == 2)
                    {
                        // Add the unit to the value and make it a string if have unit
                        std::string valueStr = std::to_string(value) + vBegin[1];
                        attrValue = StringValue(valueStr.c_str()).Copy();
                        std::cout << "Set " << className << "::" << attributeName << " to "
                                  << valueStr << std::endl;
                    }
                    else
                    {
                        attrValue = DoubleValue(value).Copy();
                    }
                }
                else
                {
                    uint32_t begin = std::stoi(vBegin[0]);
                    uint32_t end = std::stoi(vEnd[0]);
                    uint32_t value = begin + (idx / gSize) * ((double)end - (double)begin) /
                                                 (double)(groupSize - 1);
                    // Add the unit to the value and make it a string if have unit
                    std::string valueStr = std::to_string(value) + vBegin[1];
                    if (vBegin.size() == 2)
                    {
                        // Add the unit to the value and make it a string if have unit
                        std::string valueStr = std::to_string(value) + vBegin[1];
                        attrValue = StringValue(valueStr.c_str()).Copy();
                        // std::cout << "Set " << className << "::" << attributeName << " to "
                        //           << valueStr << std::endl;
                    }
                    else
                    {
                        attrValue = UintegerValue(value).Copy();
                    }
                }
            }
            else
            {
                // bool isFloat = false;
                // if (valueObj.find("isFloat") != valueObj.end())
                // {
                //     isFloat = valueObj.find("isFloat")->value().get_bool();
                // }
                bool isFloat = beginValue.is_double();
                // std::clog << "isFloat: " << isFloat << std::endl;

                if (isFloat)
                {
                    double begin = beginValue.get_double();
                    double end = endValue.get_double();
                    double value =
                        begin + (idx / gSize) * (double)(end - begin) / (double)(groupSize - 1);
                    std::cout << "Set " << className << "::" << attributeName << " to " << value
                              << std::endl;
                    attrValue = DoubleValue(value).Copy();
                }
                else
                {
                    uint32_t begin =
                        beginValue.is_uint64() ? beginValue.get_uint64() : beginValue.get_int64();
                    uint32_t end =
                        endValue.is_uint64() ? endValue.get_uint64() : endValue.get_int64();
                    uint32_t value = begin + (idx / gSize) * ((double)end - (double)begin) /
                                                 (double)(groupSize - 1);
                    std::cout << "Set " << className << "::" << attributeName << " to " << value
                              << std::endl;
                    attrValue = UintegerValue(value).Copy();
                }
            }
        }
        // If the value of the attribute is an array, direct get the value from idx
        else if (kvPair.value().is_array())
        {
            boost::json::array valueArray = kvPair.value().as_array();
            // Get the value according to the index
            boost::json::value value = valueArray[idx / gSize];
            // The value may be bool, double, int or string
            switch (value.kind())
            {
            case boost::json::kind::string:
                if (value.get_string().find("CoflowCDF ") != std::string::npos)
                {
                    std::string cdfFile = value.get_string().c_str() + 10;
                    std::unique_ptr<DcbTrafficGenApplication::TraceCdf> cdf =
                        DcbTrafficGenApplicationHelper::ConstructCdfFromFile(cdfFile);
                    helper->SetCdf(std::move(cdf));
                    attrValue = StringValue("CoflowCDF").Copy();
                }
                // If the value is a string in format "CDF XXX", set the CDF for application
                else if (value.get_string().find("CDF ") != std::string::npos)
                {
                    std::string cdfFile = value.get_string().c_str() + 4;
                    std::unique_ptr<DcbTrafficGenApplication::TraceCdf> cdf =
                        DcbTrafficGenApplicationHelper::ConstructCdfFromFile(cdfFile);
                    helper->SetCdf(std::move(cdf));
                    attrValue = StringValue("CDF").Copy();
                }
                // Otherwise if the string has a " ", treat the latter part as metadata group name
                else if (value.get_string().find(" ") != std::string::npos)
                {
                    std::string valueStr = value.get_string().c_str();
                    std::vector<std::string> vValue;
                    boost::split(vValue, valueStr, boost::is_any_of(" "), boost::token_compress_on);
                    helper->SetAppAttribute(
                        std::make_pair("MetadataGroupName", StringValue(vValue[1].c_str()).Copy()));
                    attrValue = StringValue(vValue[0].c_str()).Copy();
                }
                else
                {
                    attrValue = StringValue(value.get_string().c_str()).Copy();
                }
                std::cout << "Set " << className << "::" << attributeName << " to "
                          << value.get_string().c_str() << std::endl;
                break;
            case boost::json::kind::uint64:
                attrValue = UintegerValue(value.get_uint64()).Copy();
                std::cout << "Set " << className << "::" << attributeName << " to "
                          << value.get_uint64() << std::endl;
                break;
            case boost::json::kind::int64:
                attrValue = UintegerValue(value.get_int64()).Copy();
                std::cout << "Set " << className << "::" << attributeName << " to "
                          << value.get_int64() << std::endl;
                break;
            case boost::json::kind::bool_:
                attrValue = BooleanValue(value.get_bool()).Copy();
                std::cout << "Set " << className << "::" << attributeName << " to "
                          << value.get_bool() << std::endl;
                break;
            case boost::json::kind::double_:
                attrValue = DoubleValue(value.get_double()).Copy();
                std::cout << "Set " << className << "::" << attributeName << " to "
                          << value.get_double() << std::endl;
                break;
            default:
                NS_FATAL_ERROR("Cannot recognize the type of value");
                ;
            }
        }

        // Set to application helper
        if (className == "Congestion")
        {
            helper->SetCcAttribute(std::make_pair(attributeName, attrValue));
        }
        // else if (className == "Socket")
        // {
        //     helper->SetSocketAttributes(std::make_pair(attributeName,
        //     StringValue(valueStr.c_str()).Copy()));
        // }
        else if (className == "Application")
        {
            helper->SetAppAttribute(std::make_pair(attributeName, attrValue));
        }
        else
        {
            NS_FATAL_ERROR("Cannot recognize class name \"" << className << "\"");
        }
    }
}

/***** Utilities about CDF *****/
std::unique_ptr<DcbTrafficGenApplication::TraceCdf>
ConstructCdf(const boost::json::object& conf)
{
    std::string cdfFile = conf.find("cdfFile")->value().as_string().c_str();
    uint32_t avgSize = ConvertToUint(conf.find("avgSize")->value());
    // The double value may be interpreted as int64_t
    double scaleFactor = ConvertToDouble(conf.find("scaleFactor")->value());

    // If avgSize is specified, use it to scale the CDF
    if (avgSize != 0)
    {
        return DcbTrafficGenApplicationHelper::ConstructCdfFromFile(cdfFile, avgSize);
    }
    // If scaleFactor is specified, use it to scale the CDF
    else if (scaleFactor != 0)
    {
        return DcbTrafficGenApplicationHelper::ConstructCdfFromFile(cdfFile, scaleFactor);
    }
    // If neither is specified, use the CDF directly
    else
    {
        return DcbTrafficGenApplicationHelper::ConstructCdfFromFile(cdfFile);
    }
}

std::set<uint32_t>
GetRandomHosts(uint32_t num, uint32_t max)
{
    // using ns3's random number generator
    Ptr<UniformRandomVariable> rand = CreateObject<UniformRandomVariable>();
    // can be choce from 0 to hosts's size, and should be integer
    rand->SetAttribute("Min", DoubleValue(0));
    rand->SetAttribute("Max", DoubleValue(max));

    // make sure that no duplicate hosts are selected
    std::set<uint32_t> hosts;
    while (hosts.size() < num)
    {
        hosts.insert(rand->GetInteger());
    }
    return hosts;
}

} // namespace json_util

} // namespace ns3
