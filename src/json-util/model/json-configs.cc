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
#include "json-configs.h"

#include "ns3/json-utils.h"
#include "ns3/mtp-interface.h"

#include <fstream>
#include <iostream>
#include <map>
#include <numeric>
#include <time.h>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("JsonConfigs");

namespace json_util
{

boost::json::object
ReadConfig(std::string config_file)
{
    std::ifstream configf;
    configf.open(config_file.c_str());
    std::stringstream buf;
    buf << configf.rdbuf();
    boost::system::error_code ec;
    boost::json::object configJsonObj = boost::json::parse(buf.str()).as_object();
    if (ec.failed())
    {
        std::cout << ec.message() << std::endl;
        NS_FATAL_ERROR("Config file read error!");
    }
    return configJsonObj;
}

void
SetDefault(boost::json::object& defaultObj)
{
    for (auto kvPair : defaultObj)
    {
        // kvPair is the first level pair
        // kvPair contains {Class: {Attribute: Value}}
        std::string className(kvPair.key());
        boost::json::object subObj = kvPair.value().get_object();
        for (auto subKvPair : subObj)
        {
            std::string attributeName(subKvPair.key());
            boost::json::value value = subKvPair.value();
            switch (value.kind())
            {
            case boost::json::kind::string:
                Config::SetDefault("ns3::" + className + "::" + attributeName,
                                   StringValue(value.get_string().c_str()));
                break;
            case boost::json::kind::uint64:
                Config::SetDefault("ns3::" + className + "::" + attributeName,
                                   UintegerValue(value.get_uint64()));
                break;
            case boost::json::kind::int64:
                Config::SetDefault("ns3::" + className + "::" + attributeName,
                                   UintegerValue(value.get_int64()));
                break;
            case boost::json::kind::bool_:
                Config::SetDefault("ns3::" + className + "::" + attributeName,
                                   BooleanValue(value.get_bool()));
                break;
            case boost::json::kind::double_:
                Config::SetDefault("ns3::" + className + "::" + attributeName,
                                   DoubleValue(value.get_double()));
                break;
            default:;
            }
        }
    }
}

void
SetGlobal(boost::json::object& globalObj)
{
    // In this function, global value is allocated on heap without being freed
    // See declaration of this function for more details and take care!!!
    for (auto kvPair : globalObj)
    {
        // kvPair are {Name: Value}
        std::string name(kvPair.key());
        boost::json::value value = kvPair.value();
        switch (value.kind())
        {
        case boost::json::kind::string:
            new ns3::GlobalValue(name,
                                 name,
                                 ns3::StringValue(value.get_string().c_str()),
                                 ns3::MakeStringChecker());
            break;
        case boost::json::kind::uint64:
            new ns3::GlobalValue(name,
                                 name,
                                 ns3::UintegerValue(value.get_uint64()),
                                 ns3::MakeUintegerChecker<uint64_t>());
            break;
        case boost::json::kind::int64:
            // XXX All interger values are read as int64_t..
            new ns3::GlobalValue(name,
                                 name,
                                 ns3::UintegerValue(value.get_int64()),
                                 ns3::MakeUintegerChecker<uint64_t>());
            break;
        case boost::json::kind::bool_:
            new ns3::GlobalValue(name,
                                 name,
                                 ns3::BooleanValue(value.get_bool()),
                                 ns3::MakeBooleanChecker());
            break;
        case boost::json::kind::double_:
            new ns3::GlobalValue(name,
                                 name,
                                 ns3::DoubleValue(value.get_double()),
                                 ns3::MakeDoubleChecker<double>());
            break;
        default:;
        }
    }
}

/***** Utilities about setting seed*****/
void
SetRandomSeed(boost::json::object& configJsonObj)
{
    uint32_t seed = configJsonObj["runtimeConfig"].get_object().find("seed")->value().as_int64();
    if (seed == 0)
    {
        // Random seed
        SeedManager::SetSeed(time(NULL));
    }
    else
    {
        // Manually set seed
        SeedManager::SetSeed(seed);
    }
}

void
SetRandomSeed(uint32_t seed)
{
    if (seed == 0)
    {
        // Random seed
        SeedManager::SetSeed(time(NULL));
    }
    else
    {
        // Manually set seed
        SeedManager::SetSeed(seed);
    }
}

void
SetStopTime(boost::json::object& configJsonObj)
{
    Time stopTime = Time(
        configJsonObj["runtimeConfig"].get_object().find("stopTime")->value().as_string().c_str());
    Simulator::Stop(stopTime);
}

void
SetRuntime(boost::json::object& configJsonObj)
{
    const auto runtimeConfig =
        JsonGetObjectOrRaise(configJsonObj, "runtimeConfig", "Cannot find runtimeConfig field");
    uint32_t seed =
        JsonGetInt64OrRaise(runtimeConfig, "seed", "Cannot find seed field in runtimeConfig");
    SetRandomSeed(seed);
    JsonCallIfExistsInt<uint32_t>(runtimeConfig, "mtpThreads", [](uint32_t threads) {
        if (threads > 1)
        {
            NS_LOG_INFO("Using MTP with threads: " << threads);
            MtpInterface::Enable(threads);
        }
    });
    std::string stopTime = JsonGetStringOrRaise(runtimeConfig,
                                                "stopTime",
                                                "Cannot find stopTime field in runtimeConfig");
    Simulator::Stop(Time(stopTime));
}

} // namespace json_util

} // namespace ns3
