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

#include <boost/algorithm/string.hpp>
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

#include <gpioplus/chip.hpp>
#include <gpioplus/event.hpp>
#include <gpioplus/handle.hpp>

using namespace phosphor::logging;
using Json = nlohmann::json;

namespace ampere
{
namespace host
{
/* const */
constexpr const char* profInterface = "org.freedesktop.DBus.Properties";
constexpr const char* getMethod = "Get";

const static constexpr char* powerSService =
                            "org.openbmc.control.Power";
const static constexpr char* powerPath      =
                            "/org/openbmc/control/power0";
const static constexpr char* powerPGoodIntf =
                            "org.openbmc.control.Power";
const static constexpr char* powerPGoodProp = "pgood";

const static constexpr char* hostService   = "xyz.openbmc_project.State.Host";
const static constexpr char* hostPath    = "/xyz/openbmc_project/state/host0";
const static constexpr char* hostStateIntf = "xyz.openbmc_project.State.Host";
const static constexpr char* hostStatePro  = "CurrentHostState";
const static constexpr char* HOST_STATE_RUNNING =
    "xyz.openbmc_project.State.Host.HostState.Running";
const static constexpr char* HOST_STATE_OFF =
    "xyz.openbmc_project.State.Host.HostState.Off";
const static constexpr char* HOST_STATE_QUIESCED =
    "xyz.openbmc_project.State.Host.HostState.Quiesced";
const static constexpr char* HOST_STATE_DIAG =
    "xyz.openbmc_project.State.Host.HostState.DiagnosticMode";

const static constexpr char* HOST_RUNNING_FILE =
    "/run/openbmc/host@0-on";

static int GPIO_CHIP_ID = 0;
static int GPIO_S0_FW_BOOT_OK = 48;
static int MSG_BUFFER_LENGTH = 128;

/*
 * Time out to check the FW_BOOT_OK after GPIO PGOOD go high.
 * 120 seconds is enough to cover failover case.
 */
const static constexpr int POWERON_TIME_OUT = 120;

/* connection to sdbus */
static boost::asio::io_service io;
static std::shared_ptr<sdbusplus::asio::connection> conn;

/** @brief Parsing config JSON file  */
Json parseConfigFile(const std::string configFile)
{
    std::ifstream jsonFile(configFile);
    if (!jsonFile.is_open())
    {
        log<level::ERR>("config JSON file not found",
                        entry("FILENAME = %s", configFile.c_str()));
        throw std::exception{};
    }

    auto data = Json::parse(jsonFile, nullptr, false);
    if (data.is_discarded())
    {
        log<level::ERR>("config readings JSON parser failure",
                        entry("FILENAME = %s", configFile.c_str()));
        throw std::exception{};
    }

    return data;
}

static int parseHostStateConfiguration()
{
    static const Json empty{};
    char buff[MSG_BUFFER_LENGTH] = {'\0'};
    auto data = parseConfigFile(STATE_UPDATE_CONFIG_FILE);
    int8_t desc = -1;

    desc = data.value("gpio_chip", -1);
    if (desc < 0)
    {
        snprintf(buff, MSG_BUFFER_LENGTH, "gpio_chip configuration is invalid. Using default configuration!");
        log<level::WARNING>(buff);
    }
    else
    {
        GPIO_CHIP_ID = desc;
    }
    snprintf(buff, MSG_BUFFER_LENGTH, "gpio_chip %d\n", GPIO_CHIP_ID);
    log<level::INFO>(buff);
    desc = data.value("gpio_s0_running", -1);
    if (desc < 0)
    {
        snprintf(buff, MSG_BUFFER_LENGTH, "gpio_s0_running configuration is invalid. Using default configuration!");
        log<level::WARNING>(buff);
    }
    else
    {
        GPIO_S0_FW_BOOT_OK = desc;
    }
    snprintf(buff, MSG_BUFFER_LENGTH, "gpio_s0_running %d\n", GPIO_S0_FW_BOOT_OK);
    log<level::INFO>(buff);
    return 0;
}

static void createHostOnIndicateFile()
{
    /*
     * Create file for host instance and create in filesystem to indicate
     * to services that host is running
     */
    auto size = std::snprintf(nullptr, 0, HOST_RUNNING_FILE, 0);
    size++; /* null */
    std::unique_ptr<char[]> buf(new char[size]);
    std::snprintf(buf.get(), size, HOST_RUNNING_FILE, 0);
    std::ofstream outfile(buf.get());
    outfile.close();
    return;
}

static std::string getDbusProperty(
    std::shared_ptr<sdbusplus::asio::connection>& conn,
    const char* service, const char* path, const char* intf,
    const char* prop)
{
    auto method = conn->new_method_call(service, path,
                    profInterface, "Get");
    method.append(intf, prop);
    try
    {
        std::variant<std::string> values;
        auto reply = conn->call(method);
        reply.read(values);
        return std::get<std::string>(values);
    }
    catch (const sdbusplus::exception::SdBusError& e)
    {
        /* If property is not found simply return empty value */
    }
    return "";
}

static void setPropertyInString(
    std::shared_ptr<sdbusplus::asio::connection>& conn,
    const char* objService, const char* objPath, const char* intf,
    const char* prof, const char* state)
{
    conn->async_method_call(
        [](const boost::system::error_code ec) {
            if (ec)
                log<level::ERR>("Set: Dbus error: ");
        },
        objService,
        objPath,
        profInterface, "Set",
        intf, prof,
        std::variant<std::string>(state));
    return;
}

static void updateHostState(
    std::shared_ptr<sdbusplus::asio::connection>& conn,
    int state)
{
    if (state) {
        createHostOnIndicateFile();
        setPropertyInString(conn, hostService, hostPath,
            hostStateIntf, hostStatePro, HOST_STATE_RUNNING);
    }
    else {
        setPropertyInString(conn, hostService, hostPath,
            hostStateIntf, hostStatePro, HOST_STATE_OFF);
    }
    return;
}

static int gpioGetValue(uint32_t id, uint32_t line)
{
    gpioplus::Chip chip(id);
    gpioplus::HandleFlags handleflags(chip.getLineInfo(line).flags);
    handleflags.output = false;
    gpioplus::EventFlags eventflags;
    eventflags.falling_edge = true;
    eventflags.rising_edge = true;
    gpioplus::Event event(chip, line, handleflags, eventflags,
                            "ampere_host_state");

    return event.getValue();
}

static void hostStateChangeCheckFwBootOk(
    std::shared_ptr<sdbusplus::asio::connection>& conn, int isHostOn)
{
    int countDown = 10;
    int s0_fw_boot_ok;
next:
    try
    {
        /* Get GPIO_S0_FW_BOOT_OK GPIO */
        s0_fw_boot_ok = gpioGetValue(GPIO_CHIP_ID, GPIO_S0_FW_BOOT_OK);

        /* Change the CurrentHostState base on GPIO_S0_FW_BOOT_OK */
        if (isHostOn != s0_fw_boot_ok)
            updateHostState(conn, s0_fw_boot_ok);
    }
    catch (std::exception &e)
    {
        /* Retry if failed to request GPIO. The retry times is 10 */
        sleep(10);
        countDown--;
        log<level::ERR>("Exception when read gpio\n");
        if (countDown > 0)
            goto next;
    }
    return;
}

static void pgoodChangeUpdateHostState(
    std::shared_ptr<sdbusplus::asio::connection>& conn, int isPowerOn)
{
    int s0_fw_boot_ok = -1;
    bool contCheck = true;
    int retry = 0;
    int isCurStateOn = -1;
    std::string state;
    /* Pgood go low. Power off */
    if (!isPowerOn)
    {
        contCheck = false;
        updateHostState(conn, 0);
        return;
    }

    while (contCheck && (retry < POWERON_TIME_OUT))
    {
        contCheck = true;
        try
        {
            state = getDbusProperty(conn, hostService,
                hostPath, hostStateIntf, hostStatePro);
            if (boost::ends_with(state, "Off"))
                isCurStateOn = 0;
            else
                isCurStateOn = 1;

            /* Get GPIO_S0_FW_BOOT_OK GPIO */
            s0_fw_boot_ok = gpioGetValue(GPIO_CHIP_ID, GPIO_S0_FW_BOOT_OK);

            /* The last state does not match with FW_BOOT_OK state */
            if (s0_fw_boot_ok != isCurStateOn) {
                updateHostState(conn, s0_fw_boot_ok);
            }
            /* Power on and the host is already on */
            if (s0_fw_boot_ok)
                contCheck = false;
            else {
                retry ++;
                if (retry >= POWERON_TIME_OUT)
                    contCheck = false;
            }
            if (contCheck)
                sleep(1);
        }
        catch (std::exception &e)
        {
            log<level::ERR>("Exception when read gpio\n");
            retry ++;
            sleep(10);
        }
    }
    return;
}

static std::unique_ptr<sdbusplus::bus::match::match> signalPGoodState()
{
    return std::make_unique<sdbusplus::bus::match::match>(
        *conn,
        "type='signal',interface='" + std::string(profInterface)
            + "',path='" + std::string(powerPath) + "',arg0='"
            + std::string(powerPGoodIntf) + "'",
        [](sdbusplus::message::message& message) {
            std::string objectName;
            boost::container::flat_map<std::string, std::variant<int>>
                values;
            message.read(objectName, values);
            auto findState = values.find(powerPGoodProp);
            if (findState != values.end()) {
                pgoodChangeUpdateHostState(conn,
                    std::get<int>(findState->second));
                return;
            }
        });
}

} // namespace host
} // namespace ampere

int main(int argc, char** argv)
{
    /* Initialize dbus connection */
    boost::asio::io_service io;
    ampere::host::conn =
      std::make_shared<sdbusplus::asio::connection>(ampere::host::io);
    log<level::INFO>("Starting xyz.openbmc_project.Ampere.hostStateMonitor");
    ampere::host::conn->request_name(
            "xyz.openbmc_project.Ampere.hostStateMonitor");
    ampere::host::parseHostStateConfiguration();
    std::vector<std::unique_ptr<sdbusplus::bus::match::match>> matches;
    /* Start tracking power state */
    std::unique_ptr<sdbusplus::bus::match::match> powerMonitor =
        ampere::host::signalPGoodState();
    matches.emplace_back(std::move(powerMonitor));
    /* Synchronize CurrentHostState base on S0_BOOT_FW_OK when start */
    ampere::host::hostStateChangeCheckFwBootOk(ampere::host::conn, -1);

    /* wait for the signal */
    ampere::host::io.run();
    return 0;
}
