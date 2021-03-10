#pragma once

#include "config.h"

#include <sdbusplus/bus.hpp>
#include <sdbusplus/server.hpp>
#include <sdeventplus/clock.hpp>
#include <sdeventplus/event.hpp>
#include <sdeventplus/utility/timer.hpp>
#include <xyz/openbmc_project/Sensor/Threshold/Critical/server.hpp>
#include <xyz/openbmc_project/Sensor/Threshold/Warning/server.hpp>
#include <xyz/openbmc_project/Sensor/Value/server.hpp>

namespace phosphor
{
namespace nic
{
/* Support threshold sensor when needed */
using ValueIface = sdbusplus::xyz::openbmc_project::Sensor::server::Value;

using CriticalInterface =
    sdbusplus::xyz::openbmc_project::Sensor::Threshold::server::Critical;

using WarningInterface =
    sdbusplus::xyz::openbmc_project::Sensor::Threshold::server::Warning;

using NicIfaces =
    sdbusplus::server::object::object<ValueIface>;

class Nic : public NicIfaces
{
  public:
    Nic() = delete;
    Nic(const Nic&) = delete;
    Nic& operator=(const Nic&) = delete;
    Nic(Nic&&) = delete;
    Nic& operator=(Nic&&) = delete;
    virtual ~Nic() = default;

    /** @brief Constructs Nic
     *
     * @param[in] bus     - Handle to system dbus
     * @param[in] objPath - The Dbus path of nvme
     */
    Nic(sdbusplus::bus::bus& bus, const char* objPath) :
        NicIfaces(bus, objPath), bus(bus)
    {
    }

    /** @brief Set sensor value temperature to D-bus  */
    void setSensorValueToDbus(const int8_t value);

  private:
    sdbusplus::bus::bus& bus;
};
} // namespace nic
} // namespace phosphor
