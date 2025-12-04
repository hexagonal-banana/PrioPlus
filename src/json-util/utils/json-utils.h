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
#ifndef JSON_UTILS_H
#define JSON_UTILS_H

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/dc-topology.h"
#include "ns3/dcb-traffic-gen-application-helper.h"
#include "ns3/dcb-traffic-gen-application.h"
#include "ns3/error-model.h"
#include "ns3/flow-identifier.h"
#include "ns3/global-route-manager.h"
#include "ns3/global-value.h"
#include "ns3/internet-module.h"
#include "ns3/ipv4-static-routing-helper.h"
#include "ns3/network-module.h"
#include "ns3/packet.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/point-to-point-module.h"
#include "ns3/real-time-application.h"
#include "ns3/traffic-control-module.h"

#include <boost/json.hpp>
#include <set>

namespace ns3
{

namespace json_util
{

void PrettyPrint(std::ostream& os, const boost::json::value& jv, std::string* indent = nullptr);

// Convert boost::json::value to number.
// To use these functions, make sure you know the type of the value.
double ConvertToDouble(const boost::json::value& v);
int64_t ConvertToInt(const boost::json::value& v);
uint64_t ConvertToUint(const boost::json::value& v);

/**
 * \brief Convert a range string to a vector.
 * \param range The range string, e.g. "[1:3,5,7:]".
 * \param max The max value of the range.
 * \return A vector containing the values in the range.
 */
std::vector<uint32_t> ConvertRangeToVector(std::string range, uint32_t max);

typedef std::pair<std::string, Ptr<AttributeValue>> ConfigEntry_t;
/**
 * \brief Construct a vector of ConfigEntry_t from a json object.
 * \param configObj The json object.
 * \return A vector of ConfigEntry_t.
 */
std::unique_ptr<std::vector<ConfigEntry_t>> ConstructConfigVector(
    const boost::json::object& configObj);

/*****************************************************
 * A series of functions to extract field from json.
 *****************************************************/
inline boost::json::object::const_iterator
JsonGetFieldOrRaise(const boost::json::object& obj,
                    const std::string& field,
                    const std::string& failureMsg)
{
    boost::json::object::const_iterator subobj = obj.find(field);
    if (subobj == obj.end())
    {
        NS_FATAL_ERROR(failureMsg);
    }
    return subobj;
}

inline boost::json::object
JsonGetObjectOrRaise(const boost::json::object& obj,
                     const std::string& field,
                     const std::string& failureMsg)
{
    auto subobj = JsonGetFieldOrRaise(obj, field, failureMsg);
    return subobj->value().get_object();
}

inline int64_t
JsonGetInt64OrRaise(const boost::json::object& obj,
                    const std::string& field,
                    const std::string& failureMsg)
{
    auto subobj = JsonGetFieldOrRaise(obj, field, failureMsg);
    try
    {
        return subobj->value().as_int64();
    }
    catch (const std::exception& err)
    {
        NS_FATAL_ERROR("Failed to parse field " << field << " as an int64");
    }
}

inline double
JsonGetDoubleOrRaise(const boost::json::object& obj,
                     const std::string& field,
                     const std::string& failureMsg)
{
    auto subobj = JsonGetFieldOrRaise(obj, field, failureMsg);
    try
    {
        return subobj->value().as_double();
    }
    catch (const std::exception& err)
    {
        NS_FATAL_ERROR("Failed to parse field " << field << " as an int64");
    }
}

inline std::string
JsonGetStringOrRaise(const boost::json::object& obj,
                     const std::string& field,
                     const std::string& failureMsg)
{
    auto subobj = JsonGetFieldOrRaise(obj, field, failureMsg);
    try
    {
        return subobj->value().as_string().c_str();
    }
    catch (const std::exception& err)
    {
        NS_FATAL_ERROR("Failed to parse field " << field << " as a string");
    }
}

inline boost::json::array
JsonGetArrayOrRaise(const boost::json::object& obj,
                    const std::string& field,
                    const std::string& failureMsg)
{
    auto subobj = JsonGetFieldOrRaise(obj, field, failureMsg);
    try
    {
        return subobj->value().get_array();
    }
    catch (const std::exception& err)
    {
        NS_FATAL_ERROR("Failed to parse field " << field << " as array");
    }
}

inline bool
JsonCallIfExistsString(const boost::json::object& obj,
                       std::string field,
                       std::function<void(std::string)> callback)
{
    boost::json::object::const_iterator subobj = obj.find(field);
    if (subobj == obj.end())
    {
        return false;
    }
    callback(subobj->value().as_string().c_str());
    return true;
}

template <typename U>
bool
JsonCallIfExistsInt(const boost::json::object& obj,
                    std::string field,
                    std::function<void(U)> callback)
{
    static_assert(std::is_integral<U>::value, "Integer type required.");
    boost::json::object::const_iterator subobj = obj.find(field);
    if (subobj == obj.end())
    {
        return false;
    }
    callback(static_cast<U>(subobj->value().as_int64()));
    return true;
}

template <typename U>
bool
JsonCallIfExistsFloat(const boost::json::object& obj,
                      std::string field,
                      std::function<void(U)> callback)
{
    static_assert(std::is_floating_point<U>::value, "Floating point type required.");
    boost::json::object::const_iterator subobj = obj.find(field);
    if (subobj == obj.end())
    {
        return false;
    }
    callback(static_cast<U>(subobj->value().as_double()));
    return true;
}

inline bool
JsonCallIfExistsBool(const boost::json::object& obj,
                     std::string field,
                     std::function<void(bool)> callback)
{
    boost::json::object::const_iterator subobj = obj.find(field);
    if (subobj == obj.end())
    {
        return false;
    }
    callback(subobj->value().as_bool());
    return true;
}

inline bool
JsonCallIfExistsArray(const boost::json::object& obj,
                      std::string field,
                      std::function<void(const boost::json::array&)> callback)
{
    boost::json::object::const_iterator subobj = obj.find(field);
    if (subobj == obj.end())
    {
        return false;
    }
    try
    {
        callback(subobj->value().get_array());
        return true;
    }
    catch (const std::exception& err)
    {
        return false;
    }
}

} // namespace json_util

} // namespace ns3

#endif /* JSON_UTILS_H */