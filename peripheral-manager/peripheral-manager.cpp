#include "peripheral-manager.hpp"
#include "smbus.hpp"

#include <filesystem>
#include <map>
#include <phosphor-logging/elog-errors.hpp>
#include <phosphor-logging/log.hpp>
#include <sdbusplus/message.hpp>
#include <sstream>
#include <string>

#define MONITOR_INTERVAL_SECONDS 5
static constexpr auto configFile = "/etc/peripheral/config.json";

/* This is OCP 2.0 offset specification */
#define I2C_NIC_ADDR                            0x1f
#define I2C_NIC_SENSOR_TEMP_REG                 0x01
#define I2C_NIC_SENSOR_DEVICE_ID_LOW_REG        0xFF
#define I2C_NIC_SENSOR_DEVICE_ID_HIGH_REG       0xF1
#define I2C_NIC_SENSOR_MFR_ID_HIGH_REG          0xF0
#define I2C_NIC_SENSOR_MFR_ID_LOW_REG           0xFE

/* This is NVME specification */
#define NVME_SSD_SLAVE_ADDRESS                  0x6a
#define NVME_TEMP_REG                           0x3
#define NVME_VENDOR_REG                         0x9
#define NVME_SERIAL_NUM_REG                     0x0b
#define NVME_SERIAL_NUM_SIZE                    20

/* PCIe-SIG Vendor ID Code*/
#define VENDOR_ID_HGST                          0x1C58
#define VENDOR_ID_HYNIX                         0x1C5C
#define VENDOR_ID_INTEL                         0x8086
#define VENDOR_ID_LITEON                        0x14A4
#define VENDOR_ID_MICRON                        0x1344
#define VENDOR_ID_SAMSUNG                       0x144D
#define VENDOR_ID_SEAGATE                       0x1BB1
#define VENDOR_ID_TOSHIBA                       0x1179
#define VENDOR_ID_FACEBOOK                      0x1D9B
#define VENDOR_ID_BROARDCOM                     0x14E4
#define VENDOR_ID_QUALCOMM                      0x17CB
#define VENDOR_ID_SSSTC                         0x1E95

/* static variables */
static constexpr int SERIALNUMBER_START_INDEX   = 3;
static constexpr int SERIALNUMBER_END_INDEX     = 23;

namespace fs = std::filesystem;

namespace phosphor
{
namespace nic
{
using namespace std;
using namespace phosphor::logging;

static int nvmeMaxTemp = 0;
static const std::string nvmeMaxTempName = "_max";

static std::vector<PeripheralManager::PciePeripheral> supported = {
    {0x8119, 0x1017, "Mellanox Technologies MT27800 Family [ConnectX-5]"}
};

void PeripheralManager::setPeripheralInventoryProperties(
    bool present, const phosphor::nic::PeripheralManager::PeripheralData& peripheralData,
    const std::string& inventoryPath)
{
    util::SDBusPlus::setProperty(bus, INVENTORY_BUSNAME, inventoryPath,
                                 ITEM_IFACE, "Present", present);

    util::SDBusPlus::setProperty(bus, INVENTORY_BUSNAME, inventoryPath,
                                 ASSET_IFACE, "Model", peripheralData.name);

    util::SDBusPlus::setProperty(bus, INVENTORY_BUSNAME, inventoryPath,
                                 OPERATIONAL_STATUS_INTF, "Functional",
                                 peripheralData.functional);

    if (!present)
    {
        std::string serial = "";
        util::SDBusPlus::setProperty(bus, INVENTORY_BUSNAME, inventoryPath,
                                     ASSET_IFACE, "SerialNumber", serial);
    }
    else if (!peripheralData.serial.empty())
    {
        auto serial_str = nvmeSerialFormat(peripheralData.serial);
        util::SDBusPlus::setProperty(bus, INVENTORY_BUSNAME, inventoryPath,
                                     ASSET_IFACE, "SerialNumber", serial_str);
    }
}

/** @brief Get info over i2c */
bool PeripheralManager::getPeripheralInfobyBusID(
    PeripheralConfig& config, phosphor::nic::PeripheralManager::PeripheralData& peripheralData)
{
    phosphor::smbus::Smbus smbus;
    static std::unordered_map<int, bool> isErrorSmbus;
    int ret = 0;

    peripheralData.name = "Unknown OCP peripheral";
    peripheralData.present = false;
    peripheralData.functional = false;
    peripheralData.sensorValue = 0;
    peripheralData.serial = {};

    auto init = smbus.smbusInit(config.busID);
    if (init == -1)
    {
        if (isErrorSmbus[config.busID] != true)
        {
            log<level::ERR>("smbusInit fail!");
        }
        return false;
    }

    try
    {
        for (const auto& mux : config.muxes)
        {
            ret = smbus.smbusMuxToChan(config.busID, mux.first, 1 << mux.second);
            if (ret < 0)
            {
                goto error;
            }
        }

        /* Read peripheral vendor and peripheral id */
        uint8_t mfrIdHigh = smbus.smbusReadByteData(
            config.busID, I2C_NIC_ADDR, I2C_NIC_SENSOR_MFR_ID_HIGH_REG);

        uint8_t mfrIdLow = smbus.smbusReadByteData(
            config.busID, I2C_NIC_ADDR, I2C_NIC_SENSOR_MFR_ID_LOW_REG);
        /* Manufacture id is 2 bytes */
        auto mfrId = mfrIdLow | (mfrIdHigh << 8);

        /* Read device id */
        uint8_t devIdHigh = smbus.smbusReadByteData(
            config.busID, I2C_NIC_ADDR, I2C_NIC_SENSOR_DEVICE_ID_HIGH_REG);
        uint8_t devIdLow = smbus.smbusReadByteData(
            config.busID, I2C_NIC_ADDR, I2C_NIC_SENSOR_DEVICE_ID_LOW_REG);
        auto deviceId = devIdLow | (devIdHigh << 8);

        auto result = std::find_if(
            supported.begin(), supported.end(),
            [deviceId, mfrId](PeripheralManager::PciePeripheral peripheral) {
                return (peripheral.deviceID == deviceId && peripheral.vendorID == mfrId);
            });

        /* Found supported peripheral */
        if (result != supported.end())
        {
            uint8_t value = smbus.smbusReadByteData(config.busID, I2C_NIC_ADDR,
                                                    I2C_NIC_SENSOR_TEMP_REG);
            if (value != 0xff)
            {
                peripheralData.present = true;
                peripheralData.sensorValue = value;
                peripheralData.deviceId = deviceId;
                peripheralData.mfrId = mfrId;
                peripheralData.name = result->deviceName;
                peripheralData.functional = true;
            }
        }
    }
    catch (const std::exception& e)
    {
        goto error;
    }

    /* Done, close the bus */
    smbus.smbusClose(config.busID);
    return true;

error:
    /* Close the bus */
    smbus.smbusClose(config.busID);
    return false;

}

/** @brief Get NVMe info over smbus  */
bool PeripheralManager::getNVMeInfobyBusID(
    PeripheralConfig& config, phosphor::nic::PeripheralManager::PeripheralData& peripheralData)
{
    static std::unordered_map<int, bool> isErrorSmbus;
    phosphor::smbus::Smbus smbus;
    int ret = 0;
    std::vector<uint8_t> tmp;
    uint8_t temp = 0;

    peripheralData.name = "";
    peripheralData.present = false;
    peripheralData.functional = false;
    peripheralData.sensorValue = 0;
    peripheralData.serial = {};

    auto init = smbus.smbusInit(config.busID);
    if (init == -1)
    {
        if (isErrorSmbus[config.busID] != true)
        {
            log<level::ERR>("smbusInit fail!");
        }
        return false;
    }

    try
    {
        for (const auto& mux : config.muxes)
        {
            ret = smbus.smbusMuxToChan(config.busID, mux.first, 1 << mux.second);
            if (ret < 0)
            {
                goto error;
            }
        }

        /* Read NVME temp data */
        auto tempValue = smbus.smbusReadByteData(config.busID, NVME_SSD_SLAVE_ADDRESS,
                                       NVME_TEMP_REG);
        if (tempValue < 0)
        {
            goto error;
        }

        /* Read VendorID 2 bytes 9-10 */
        int value = smbus.smbusReadWordData(
            config.busID, NVME_SSD_SLAVE_ADDRESS, NVME_VENDOR_REG);

        if (value < 0)
        {
            goto error;
        }
        int vendorId = (value & 0xFF00) >> 8 | (value & 0xFF) << 8;

        /* Read SerialID 20 bytes 11-31 */
        for (int count = 0; count < NVME_SERIAL_NUM_SIZE; count++)
        {
            temp = smbus.smbusReadByteData(config.busID, NVME_SSD_SLAVE_ADDRESS,
                                           NVME_SERIAL_NUM_REG + count);
            if (temp < 0)
            {
                goto error;
            }
            tmp.emplace_back(temp);
        }

        if (vendorId > 0)
        {
            peripheralData.name = nvmeNameFormat(vendorId);
            peripheralData.present = true;
            peripheralData.functional = true;
            peripheralData.sensorValue = tempValue;
            peripheralData.mfrId = vendorId;
            peripheralData.serial.insert(peripheralData.serial.end(), tmp.begin(), tmp.end());

            /* Update the NVME maximum temp value */
            if (tempValue > nvmeMaxTemp)
            {
                nvmeMaxTemp = tempValue;
            }
        }
    }
    catch (const std::exception& e)
    {
        goto error;
    }

    /* switch the mux back */
    for (auto it = config.muxes.rbegin(); it != config.muxes.rend(); ++it)
    {
        ret = smbus.smbusMuxToChan(config.busID, it->first, 0);
        if (ret < 0)
        {
            goto error;
        }
    }

    /* Done, close the bus */
    smbus.smbusClose(config.busID);
    return true;

error:
    smbus.smbusClose(config.busID);
    return false;
}

std::string PeripheralManager::nvmeSerialFormat(std::vector<uint8_t> serial)
{
    std::stringstream ss;
    ss.str("");
    if (!serial.empty())
    {
        for (auto it = serial.begin(); it != serial.end(); it++)
        {
            ss << *it;
        }
    }

    return ss.str();
}

std::string PeripheralManager::nvmeNameFormat(uint16_t vendorId)
{
    std::stringstream ss;
    ss.str("");

    switch (vendorId)
    {
        case VENDOR_ID_HGST:
            ss << "HGST (" << std::hex << std::setw(4) << std::setfill('0')
               << vendorId << ")";
            break;
        case VENDOR_ID_HYNIX:
            ss << "Hynix (" << std::hex << std::setw(4) << std::setfill('0')
               << vendorId << ")";
            break;
        case VENDOR_ID_INTEL:
            ss << "Intel (" << std::hex << std::setw(4) << std::setfill('0')
               << vendorId << ")";
            break;
        case VENDOR_ID_LITEON:
            ss << "Lite-on (" << std::hex << std::setw(4) << std::setfill('0')
               << vendorId << ")";
            break;
        case VENDOR_ID_MICRON:
            ss << "Micron (" << std::hex << std::setw(4) << std::setfill('0')
               << vendorId << ")";
            break;
        case VENDOR_ID_SAMSUNG:
            ss << "Samsung (" << std::hex << std::setw(4) << std::setfill('0')
               << vendorId << ")";
            break;
        case VENDOR_ID_SEAGATE:
            ss << "Seagate (" << std::hex << std::setw(4) << std::setfill('0')
               << vendorId << ")";
            break;
        case VENDOR_ID_TOSHIBA:
            ss << "Toshiba (" << std::hex << std::setw(4) << std::setfill('0')
               << vendorId << ")";
            break;
        case VENDOR_ID_FACEBOOK:
            ss << "Facebook (" << std::hex << std::setw(4) << std::setfill('0')
               << vendorId << ")";
            break;
        case VENDOR_ID_BROARDCOM:
            ss << "Broadcom (" << std::hex << std::setw(4) << std::setfill('0')
               << vendorId << ")";
            break;
        case VENDOR_ID_QUALCOMM:
            ss << "Qualcomm (" << std::hex << std::setw(4) << std::setfill('0')
               << vendorId << ")";
            break;
        case VENDOR_ID_SSSTC:
            ss << "SSSTC (" << std::hex << std::setw(4) << std::setfill('0')
               << vendorId << ")";
            break;
        default:
            ss << "Unknown (" << std::hex << std::setw(4) << std::setfill('0')
               << vendorId << ")";
            break;
    }

    return ss.str();
}

void PeripheralManager::run()
{
    init();

    std::function<void()> callback(std::bind(&PeripheralManager::read, this));
    try
    {
        u_int64_t interval = MONITOR_INTERVAL_SECONDS * 1000000;
        _timer.restart(std::chrono::microseconds(interval));
    }
    catch (const std::exception& e)
    {
        log<level::ERR>("Error in polling loop. "),
            entry("ERROR = %s", e.what());
    }
}

/** @brief Parsing Peripheral config JSON file  */
Json parseSensorConfig()
{
    std::ifstream jsonFile(configFile);
    if (!jsonFile.is_open())
    {
        log<level::ERR>("Peripheral config JSON file not found");
    }

    auto data = Json::parse(jsonFile, nullptr, false);
    if (data.is_discarded())
    {
        log<level::ERR>("Peripheral config readings JSON parser failure");
    }

    return data;
}

/** @brief Obtain the initial configuration value of Peripheral  */
std::vector<phosphor::nic::PeripheralManager::PeripheralConfig>
    PeripheralManager::getConfig()
{
    std::vector<phosphor::nic::PeripheralManager::PeripheralConfig> peripheralConfigs;

    try
    {
        auto data = parseSensorConfig();
        static const std::vector<Json> empty{};

        /* Read and parse 'ocp' config */
        std::vector<Json> readings = data.value("ocp", empty);
        parseConfig(readings, OCP, peripheralConfigs);

        /* Read and parse 'nmve' config */
        readings = data.value("nvme", empty);
        parseConfig(readings, NVME, peripheralConfigs);
    }
    catch (const Json::exception& e)
    {
        log<level::ERR>("Json Exception caught."), entry("MSG: %s", e.what());
    }

    return peripheralConfigs;
}

void PeripheralManager::parseConfig(
    std::vector<Json> readings, phosphor::nic::PeripheralManager::PeripheralType type,
    std::vector<phosphor::nic::PeripheralManager::PeripheralConfig>& peripheralConfigs)
{
    static const std::vector<Json> empty{};

    if (!readings.empty())
    {
        for (const auto& instance : readings)
        {
            phosphor::nic::PeripheralManager::PeripheralConfig peripheralConfig;
            uint8_t index = instance.value("Index", 0);

            int busID = instance.value("BusId", 0);
            peripheralConfig.index = std::to_string(index);
            peripheralConfig.busID = busID;
            peripheralConfig.type = type;
            peripheralConfig.id = std::to_string(type) + "_" + std::to_string(index);

            std::vector<Json> muxes = instance.value("Muxes", empty);
            if (!muxes.empty())
            {
                for (const auto& mux : muxes)
                {
                    std::string muxAddr = mux["MuxAddress"].get<std::string>();
                    std::string channelId = mux["Channel"].get<std::string>();

                    uint8_t address = std::strtoul(muxAddr.c_str(), 0, 16) & 0xFF;
                    int channel = std::strtoul(channelId.c_str(), 0, 16) & 0xFF;
                    auto p = std::make_pair(address, channel);

                    peripheralConfig.muxes.push_back(p);
                }
            }

            peripheralConfigs.push_back(peripheralConfig);
        }
    }
}

void PeripheralManager::createPeripheralInventory()
{
    using Properties = std::map<std::string, std::variant<std::string, bool>>;
    using Interfaces = std::map<std::string, Properties>;

    std::string inventoryPath;
    std::map<sdbusplus::message::object_path, Interfaces> obj;

    for (const auto config : configs)
    {
        if (config.type == OCP)
        {
            inventoryPath = "/system/chassis/motherboard/peripheral" + config.index;
        }
        else if (config.type == NVME)
        {
            inventoryPath = "/system/chassis/motherboard/nvme" + config.index;
        }

        obj = {{ inventoryPath, {{ITEM_IFACE, {}}, {OPERATIONAL_STATUS_INTF, {}},
                {ASSET_IFACE, {}}},
        }};

        util::SDBusPlus::CallMethod(bus, INVENTORY_BUSNAME, INVENTORY_NAMESPACE,
                                    INVENTORY_MANAGER_IFACE, "Notify", obj);
    }
}

void PeripheralManager::init()
{
    createPeripheralInventory();
}

void PeripheralManager::readPeripheralData(PeripheralConfig& config)
{
    std::string inventoryPath;
    std::string objPath;
    PeripheralData peripheralData;
    bool success = 0;

    if (config.type == OCP)
    {
        inventoryPath = PERIPHERAL_INVENTORY_PATH + config.index;
        objPath = PERIPHERAL_OBJ_PATH + config.index;
        success = getPeripheralInfobyBusID(config, peripheralData);
    }
    else if (config.type == NVME)
    {
        inventoryPath = NVME_INVENTORY_PATH + config.index;
        objPath = NVME_OBJ_PATH + config.index;

        success = getNVMeInfobyBusID(config, peripheralData);
    }

    if (success && peripheralData.present)
    {
        auto result = peripherals.find(config.id);

        if (result == peripherals.end())
        {
            auto peripheral =
                std::make_shared<phosphor::nic::Nic>(bus, objPath.c_str());
            peripherals.emplace(config.id, peripheral);

            setPeripheralInventoryProperties(peripheralData.present, peripheralData,
                                       inventoryPath);
            peripheral->setSensorValueToDbus(peripheralData.sensorValue);
        }
        else
        {
            setPeripheralInventoryProperties(peripheralData.present, peripheralData,
                                       inventoryPath);
            result->second->setSensorValueToDbus(peripheralData.sensorValue);
        }
    }
    else
    {
        peripherals.erase(config.id);
    }
}

void PeripheralManager::nvmeMaxTempSensor()
{
    std::string objPath;
    std::string sensorName;

    sensorName = std::to_string(NVME) + nvmeMaxTempName;
    auto result = peripherals.find(sensorName);
    if (result == peripherals.end())
    {
        objPath = NVME_OBJ_PATH + nvmeMaxTempName;
        auto peripheral = std::make_shared<phosphor::nic::Nic>(bus, objPath.c_str());

        peripherals.emplace(sensorName, peripheral);
        peripheral->setSensorValueToDbus(nvmeMaxTemp);
    }
    else
    {
        result->second->setSensorValueToDbus(nvmeMaxTemp);
    }
}

/** @brief Monitor every one second  */
void PeripheralManager::read()
{
    nvmeMaxTemp = 0;
    for (auto config : configs)
    {
        std::string inventoryPath;

        /* set default for each config */
        if (config.type == OCP)
        {
            inventoryPath = PERIPHERAL_INVENTORY_PATH + config.index;
        }
        else if (config.type == NVME)
        {
            inventoryPath = NVME_INVENTORY_PATH + config.index;
        }

        PeripheralData peripheralData = PeripheralData();
        peripheralData.name = "";
        peripheralData.present = false;
        peripheralData.functional = false;
        peripheralData.sensorValue = 0;
        peripheralData.serial = {};

        setPeripheralInventoryProperties(false, peripheralData, inventoryPath);
        readPeripheralData(config);

        /* Update nvme max temp sensor */
        nvmeMaxTempSensor();
    }
}
} // namespace nic
} // namespace phosphor
