/*
// Copyright 2021 Ampere Computing LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
*/

#include <boost/algorithm/string/predicate.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/container/flat_map.hpp>
#include "config.hpp"
#include <nlohmann/json.hpp>
#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/bus.hpp>
#include <sdbusplus/bus/match.hpp>
#include <sdbusplus/message.hpp>

#include <phosphor-logging/elog-errors.hpp>
#include <phosphor-logging/elog.hpp>
#include <phosphor-logging/log.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <regex>

using namespace phosphor::logging;
using Json = nlohmann::json;
namespace fs = std::filesystem;

namespace ampere
{
namespace binder
{
/* const */
constexpr const char* profInterface = "org.freedesktop.DBus.Properties";

const static constexpr char* chassisPath      =
                            "/xyz/openbmc_project/state/chassis0";
const static constexpr char* chassisStateIntf =
                            "xyz.openbmc_project.State.Chassis";
const static constexpr char* chassisTransProp = "RequestedPowerTransition";

const static constexpr char* hostService   = "xyz.openbmc_project.State.Host";
const static constexpr char* hostPath    = "/xyz/openbmc_project/state/host0";
const static constexpr char* hostStateIntf = "xyz.openbmc_project.State.Host";
const static constexpr char* hostStateProp  = "CurrentHostState";
const static constexpr char* hostTransProp = "RequestedHostTransition";
const static constexpr int DEFAULT_BIND_DELAY = 0;
const static constexpr int DEFAULT_UNBIND_DELAY = 0;
const static constexpr int MSG_BUFFER_LENGTH = 256;

static Json hostDrivers;

/* connection to sdbus */
static boost::asio::io_service io;
static std::shared_ptr<sdbusplus::asio::connection> conn;

static bool fileExists(const fs::path& p, fs::file_status s = fs::file_status{})
{
    std::cout << p;
    if(fs::status_known(s) ? fs::exists(s) : fs::exists(p)) {
        return 1;
    }

    return 0;
}

/** @brief Parsing config JSON file  */
Json parseConfigFile(const std::string configFile)
{
    std::ifstream jsonFile(configFile);
    if (!jsonFile.is_open()) {
        log<level::ERR>("config JSON file not found",
                        entry("FILENAME = %s", configFile.c_str()));
        throw std::exception{};
    }

    auto data = Json::parse(jsonFile, nullptr, false);
    if (data.is_discarded()) {
        log<level::ERR>("config readings JSON parser failure",
                        entry("FILENAME = %s", configFile.c_str()));
        throw std::exception{};
    }

    return data;
}

static int parseConfiguration()
{
    static const Json empty{};
    auto data = parseConfigFile(CONFIG_FILE);

    hostDrivers = data.value("hostDrivers", empty);
    if (hostDrivers.empty()) {
        log<level::WARNING>("hostDrivers is not configured!");
        return -1;
    }

    return 0;
}

static int bindHostDrivers(Json js, bool bind)
{
    static const Json empty{};
    char buff[MSG_BUFFER_LENGTH] = {"\0"};
    int bindDelay, unbindDelay;
    std::string cmd = "";
    u_int8_t cnt;

    if (js.empty()) {
        log<level::WARNING>("hostDrivers is not configured.");
        return -1;
    }

    bindDelay = js.value("bindDelay", DEFAULT_BIND_DELAY);
    unbindDelay = js.value("unbindDelay", DEFAULT_UNBIND_DELAY);
    auto drivers = js.value("drivers", empty);

    if (drivers.empty()) {
        log<level::WARNING>("Drivers setting of hostDrivers is not configured.");
        return -1;
    }

    if (bind && (bindDelay > 0)) {
        usleep(bindDelay*1000);
    }

    if (!bind && (unbindDelay > 0)) {
        usleep(unbindDelay*1000);
    }

    for (auto& j : drivers) {
        cnt++;
        /* Get parameter dbus sensor descriptor */
        std::string name = j.value("name", "");
        std::string path = j.value("path", "");

        if (name.empty()) {
            snprintf(buff, MSG_BUFFER_LENGTH, "Host driver %dth name is empty.", cnt);
            log<level::WARNING>(buff);
            continue;
        }

        if (path.empty()) {
            snprintf(buff, MSG_BUFFER_LENGTH, "Host driver %dth path is empty.", cnt);
            log<level::WARNING>(buff);
            continue;
        }

        if (!fileExists(path)) {
            snprintf(buff, MSG_BUFFER_LENGTH, "Path %s is invalid.", path.c_str());
            log<level::WARNING>(buff);
            continue;
        }
        cmd = "echo " + name + " > " + path;
        if (bind) {
            if (fileExists(path + name)) {
                snprintf(buff, MSG_BUFFER_LENGTH, "%s : is already binded!\n", name.c_str());
                log<level::INFO>(buff);
                continue;
            }
            cmd = cmd + "bind";
            snprintf(buff, MSG_BUFFER_LENGTH, "Calling %s.", cmd.c_str());
            log<level::INFO>(buff);
            std::system(cmd.c_str());
        }
        else {
            if (!fileExists(path + name)) {
                snprintf(buff, MSG_BUFFER_LENGTH, "%s : is already unbinded!\n", name.c_str());
                log<level::INFO>(buff);
                continue;
            }
            cmd = cmd + "unbind";
            snprintf(buff, MSG_BUFFER_LENGTH, "Calling %s.", cmd.c_str());
            log<level::INFO>(buff);
            std::system(cmd.c_str());
        }
    }
    return 1;
}

static std::unique_ptr<sdbusplus::bus::match::match> signalHostTransitionState()
{
    return std::make_unique<sdbusplus::bus::match::match>(
        *conn,
        "type='signal',interface='" + std::string(profInterface)
            + "',path='" + std::string(hostPath) + "',arg0='"
            + std::string(hostStateIntf) + "'",
        [](sdbusplus::message::message& message) {
            std::string objectName;
            boost::container::flat_map<std::string, std::variant<std::string>>
                values;
            message.read(objectName, values);
            auto findState = values.find(hostTransProp);
            if (findState != values.end()) {
                bool toOff = boost::ends_with(
                        std::get<std::string>(findState->second), "Off");
                if (toOff) {
                    log<level::INFO>("The host is going to off. Unbind hwmon driver.\n");
                    bindHostDrivers(hostDrivers, 0);
                }
                return;
            }
            findState = values.find(hostStateProp);
            if (findState != values.end()) {
                bool toOff = boost::ends_with(
                        std::get<std::string>(findState->second), "Off");
                if (!toOff) {
                    log<level::INFO>("The host is going to On. Bind hwmon driver.\n");
                    bindHostDrivers(hostDrivers, 1);
                }
                return;
            }
        });
}

static std::unique_ptr<sdbusplus::bus::match::match> signalChassisTransitionState()
{
    return std::make_unique<sdbusplus::bus::match::match>(
        *conn,
        "type='signal',interface='" + std::string(profInterface)
            + "',path='" + std::string(chassisPath) + "',arg0='"
            + std::string(chassisStateIntf) + "'",
        [](sdbusplus::message::message& message) {
            std::string objectName;
            boost::container::flat_map<std::string, std::variant<std::string>>
                values;
            message.read(objectName, values);
            auto findState = values.find(chassisTransProp);
            if (findState != values.end()) {
                bool toOff = boost::ends_with(
                        std::get<std::string>(findState->second), "Off");
                if (toOff) {
                    log<level::INFO>("The chassis is going to off. Unbind hwmon driver.\n");
                    bindHostDrivers(hostDrivers, 0);
                }
                return;
            }
        });
}

} // namespace binder
} // namespace ampere

int main(int argc, char** argv)
{
    boost::asio::io_service io;
    ampere::binder::conn =
      std::make_shared<sdbusplus::asio::connection>(ampere::binder::io);
    ampere::binder::conn->request_name(
            "xyz.openbmc_project.AmpDriverBinder.service");
    ampere::binder::parseConfiguration();
    std::vector<std::unique_ptr<sdbusplus::bus::match::match>> matches;
    std::unique_ptr<sdbusplus::bus::match::match> powerMonitor =
        ampere::binder::signalHostTransitionState();
    matches.emplace_back(std::move(powerMonitor));
    powerMonitor = ampere::binder::signalChassisTransitionState();
    matches.emplace_back(std::move(powerMonitor));

    /* wait for the signal */
    ampere::binder::io.run();
    return 0;
}
