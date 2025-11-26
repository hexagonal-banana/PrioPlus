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
#include "json-utils.h"

#include "ns3/fifo-queue-disc-ecn.h"
#include "ns3/rocev2-hpcc.h"
#include "ns3/rocev2-swift.h"
#include "ns3/rocev2-timely.h"

#include <boost/algorithm/string.hpp>
#include <boost/json/src.hpp>
#include <fstream>
#include <iostream>
#include <map>
#include <numeric>
#include <time.h>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("JsonUtils");

namespace json_util
{

// Copy from boost's example
void
PrettyPrint(std::ostream& os, const boost::json::value& jv, std::string* indent)
{
    std::string indent_;
    if (!indent)
    {
        indent = &indent_;
    }
    switch (jv.kind())
    {
    case boost::json::kind::object: {
        os << "{\n";
        indent->append(4, ' ');
        const auto& obj = jv.get_object();
        if (!obj.empty())
        {
            auto it = obj.begin();
            for (;;)
            {
                os << *indent << boost::json::serialize(it->key()) << " : ";
                PrettyPrint(os, it->value(), indent);
                if (++it == obj.end())
                {
                    break;
                }
                os << ",\n";
            }
        }
        os << "\n";
        indent->resize(indent->size() - 4);
        os << *indent << "}";
        break;
    }

    case boost::json::kind::array: {
        os << "[\n";
        indent->append(4, ' ');
        const auto& arr = jv.get_array();
        if (!arr.empty())
        {
            auto it = arr.begin();
            for (;;)
            {
                os << *indent;
                PrettyPrint(os, *it, indent);
                if (++it == arr.end())
                {
                    break;
                }
                os << ",\n";
            }
        }
        os << "\n";
        indent->resize(indent->size() - 4);
        os << *indent << "]";
        break;
    }

    case boost::json::kind::string: {
        os << boost::json::serialize(jv.get_string());
        break;
    }

    case boost::json::kind::uint64:
        os << jv.get_uint64();
        break;

    case boost::json::kind::int64:
        os << jv.get_int64();
        break;

    case boost::json::kind::double_:
        os << jv.get_double();
        break;

    case boost::json::kind::bool_:
        if (jv.get_bool())
        {
            os << "true";
        }
        else
        {
            os << "false";
        }
        break;

    case boost::json::kind::null:
        os << "null";
        break;
    }

    if (indent->empty())
    {
        os << "\n";
    }
}

double
ConvertToDouble(const boost::json::value& v)
{
    switch (v.kind())
    {
    case boost::json::kind::uint64:
        return v.get_uint64();
    case boost::json::kind::int64:
        return v.get_int64();
    case boost::json::kind::double_:
        return v.get_double();
    default:
        NS_FATAL_ERROR("Cannot convert to double");
    }
}

int64_t
ConvertToInt(const boost::json::value& v)
{
    switch (v.kind())
    {
    case boost::json::kind::uint64:
        return v.get_uint64();
    case boost::json::kind::int64:
        return v.get_int64();
    case boost::json::kind::double_:
        // If the value is read as double, it may be like 0.999 which should be converted to 1
        // To avoid this, we should convert the double to the nearest integer
        // Note that we need to consider the negative value
        if (v.get_double() < 0)
        {
            return (int64_t)(v.get_double() - 0.5);
        }
        else
        {
            return (int64_t)(v.get_double() + 0.5);
        }
    default:
        NS_FATAL_ERROR("Cannot convert to uint64_t");
    }
}

uint64_t
ConvertToUint(const boost::json::value& v)
{
    switch (v.kind())
    {
    case boost::json::kind::uint64:
        return v.get_uint64();
    case boost::json::kind::int64:
        // If the value is negative, we cannot convert it to uint64_t
        if (v.get_int64() < 0)
        {
            NS_FATAL_ERROR("Cannot convert to uint64_t");
        }
        return v.get_int64();
    case boost::json::kind::double_:
        // If the value is negative, we cannot convert it to uint64_t
        if (v.get_double() < 0)
        {
            NS_FATAL_ERROR("Cannot convert to uint64_t");
        }
        // If the value is read as double, it may be like 0.999 which should be converted to 1
        // To avoid this, we should convert the double to the nearest integer
        return (uint64_t)(v.get_double() + 0.5);
    default:
        NS_FATAL_ERROR("Cannot convert to uint64_t");
    }
}

std::vector<uint32_t>
ConvertRangeToVector(std::string range, uint32_t max)
{
    /**
     * The nodes is a string in the format of "[x,y:z]" as the index of the nodes to install the
     * application. For example, "[0:3]" means install the application on nodes 0, 1, 2
     * "[0:]" means install the application on nodes 0, 1, 2, ..., N-1
     * "[:3]" means install the application on nodes 0, 1, 2
     * "[0,2,4]" means install the application on nodes 0, 2, 4
     * "[0,2,4:]" means install the application on nodes 0, 2, 4, ..., N-1
     */
    std::vector<uint32_t> vRange;
    // Remove the "[" and "]" at the beginning and end of the string
    std::string rangeTrimed = range.substr(1, range.size() - 2);
    std::vector<std::string> vRangeStrs;
    boost::split(vRangeStrs, rangeTrimed, boost::is_any_of(",")); // split the string by ","
    for (auto sRange : vRangeStrs)
    {
        // sRange is a string in the format of "x:y" or "x"
        // If y is not specified, i.e., "x:", then y is set to -1
        // First check if the string contains ":"
        if (sRange.find(":") == std::string::npos)
        {
            // If not, then the string is just a number
            vRange.push_back(std::stoi(sRange));
            continue;
        }
        else
        {
            // If yes, then the string is in the format of "x:y"
            // Split the string by ":"
            std::vector<std::string> vRangeBound;
            boost::split(vRangeBound, sRange, boost::is_any_of(":"));
            uint32_t startNodeIndex = std::stoi(vRangeBound[0]);
            // If y is not specified, then y is set to the number of hosts
            // Note that "[5:]" will be split into "5" and "", so we need to check if vRangeBound[1]
            // is ""
            uint32_t endNodeIndex = vRangeBound[1] == "" ? max : std::stoi(vRangeBound[1]);

            for (uint32_t nodeIndex = startNodeIndex; nodeIndex < endNodeIndex; ++nodeIndex)
            {
                vRange.push_back(nodeIndex);
            }
        }
    }

    return vRange;
}

std::unique_ptr<std::vector<ConfigEntry_t>>
ConstructConfigVector(const boost::json::object& configObj)
{
    std::unique_ptr<std::vector<ConfigEntry_t>> configVector =
        std::make_unique<std::vector<ConfigEntry_t>>();
    // Each value is .Copy() to get Ptr<AttributeValue> as it is forbidden to construct directly
    for (auto kvPair : configObj)
    {
        std::string name(kvPair.key());
        boost::json::value value = kvPair.value();
        switch (value.kind())
        {
        case boost::json::kind::string:
            configVector->push_back(
                std::make_pair(name, StringValue(value.get_string().c_str()).Copy()));
            break;
        case boost::json::kind::uint64:
            configVector->push_back(std::make_pair(name, UintegerValue(value.get_uint64()).Copy()));
            break;
        case boost::json::kind::int64:
            configVector->push_back(std::make_pair(name, UintegerValue(value.get_int64()).Copy()));
            break;
        case boost::json::kind::bool_:
            configVector->push_back(std::make_pair(name, BooleanValue(value.get_bool()).Copy()));
            break;
        case boost::json::kind::double_:
            configVector->push_back(std::make_pair(name, DoubleValue(value.get_double()).Copy()));
            break;
        default:;
        }
    }
    return configVector;
}

} // namespace json_util

} // namespace ns3
