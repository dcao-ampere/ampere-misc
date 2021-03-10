#pragma once

#include "config.h"
#include "peripherals.hpp"
#include "sdbusplus.hpp"

#include <fstream>
#include <sdbusplus/bus.hpp>
#include <sdbusplus/server.hpp>
#include <sdbusplus/server/object.hpp>
#include <sdeventplus/clock.hpp>
#include <sdeventplus/event.hpp>
#include <sdeventplus/utility/timer.hpp>
#include <nlohmann/json.hpp>

using Json = nlohmann::json;

namespace phosphor
{
namespace nic
{

/** @class PeripheralManager
 *  @brief PeripheralManager manager implementation.
 */
class PeripheralManager
{
  public:
    PeripheralManager() = delete;
    PeripheralManager(const PeripheralManager&) = delete;
    PeripheralManager& operator=(const PeripheralManager&) = delete;
    PeripheralManager(PeripheralManager&&) = delete;
    PeripheralManager& operator=(PeripheralManager&&) = delete;

    /** @brief Constructs PeripheralManager
     *
     * @param[in] bus     - Handle to system dbus
     * @param[in] objPath - The dbus path of nic
     */
    PeripheralManager(sdbusplus::bus::bus& bus) :
        bus(bus), _event(sdeventplus::Event::get_default()),
        _timer(_event, std::bind(&PeripheralManager::read, this))
    {
        // read json file
        configs = getConfig();
    }

    /*
     * Structure for keeping peripheral information
     *
     * */
    struct PciePeripheral
    {
        uint32_t vendorID;
        uint32_t deviceID;
        std::string deviceName;
    };

    /**
     * Peripheral types
     */
    enum PeripheralType
    {
        OCP=0,
        NVME
    };

    /**
     * Structure for keeping nic configure data required by nic monitoring
     */
    struct PeripheralConfig
    {
        std::string id;
        std::string index;
        int busID;
        std::vector<std::pair<uint8_t, int>> muxes;
        phosphor::nic::PeripheralManager::PeripheralType type;
    };

    /**
     * Structure for keeping nic data required by nic monitoring
     */
    struct PeripheralData
    {
        bool present;
        bool functional;
        int8_t remoteTemp;
        int8_t sensorValue;
        int16_t mfrId;
        int16_t deviceId;
        std::string name;
        std::vector<uint8_t> serial;
    };

    /** @brief Setup polling timer in a sd event loop and attach to D-Bus
     *         event loop.
     */
    void run();

    /** @brief save the peripheral objects */
    std::unordered_map<std::string, std::shared_ptr<phosphor::nic::Nic>> peripherals;

    /** @brief Set inventory properties of nic */
    void setPeripheralInventoryProperties(
        bool present, const phosphor::nic::PeripheralManager::PeripheralData& peripheralData,
        const std::string& inventoryPath);

    /** @brief Create inventory of nic or nvme */
    void createPeripheralInventory();

    /** @brief read and update data to dbus */
    void readPeripheralData(PeripheralConfig& config);

  private:
    /** @brief sdbusplus bus client connection. */
    sdbusplus::bus::bus& bus;
    /** @brief the Event Loop structure */
    sdeventplus::Event _event;
    /** @brief Read Timer */
    sdeventplus::utility::Timer<sdeventplus::ClockId::Monotonic> _timer;

    std::vector<phosphor::nic::PeripheralManager::PeripheralConfig> configs;

    /** @brief Set up initial configuration value */
    void init();

    /** @brief Monitor peripheral every one second  */
    void read();

    /** @brief Get peripheral configuration */
    std::vector<phosphor::nic::PeripheralManager::PeripheralConfig> getConfig();

    /** @brief Parse the peripheral data from json string */
    void parseConfig(std::vector<Json> readings,
            phosphor::nic::PeripheralManager::PeripheralType type,
            std::vector<phosphor::nic::PeripheralManager::PeripheralConfig> &peripheralConfigs);

    /** @brief Read peripheral info via I2C */
    bool getPeripheralInfobyBusID(PeripheralConfig& config,
                    phosphor::nic::PeripheralManager::PeripheralData& peripheralData);

    /** @brief Read NVME info via I2C */
    bool getNVMeInfobyBusID(
        PeripheralConfig& config, phosphor::nic::PeripheralManager::PeripheralData& peripheralData);

    /** @brief Update nvme max temp sensor */
    void nvmeMaxTempSensor();

    std::string nvmeSerialFormat(std::vector<uint8_t> serial);
    std::string nvmeNameFormat(uint16_t vendorId);
};
} // namespace nic
} // namespace phosphor
