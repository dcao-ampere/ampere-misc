#pragma once

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace phosphor
{
namespace smbus
{

class Smbus
{
  public:
    Smbus(){};

    int openI2cDev(int i2cbus, char* filename, size_t size, int quiet);
    int smbusInit(int smbus_num);
    void smbusClose(int smbus_num);

    int smbusMuxToChan(int smbus_num, int8_t addr, uint8_t chan);
    uint8_t smbusReadByteData(int smbus_num, int8_t addr, uint8_t offset);
    int32_t smbusReadWordData(int smbus_num, int8_t addr, uint8_t offset);
};

} // namespace smbus
} // namespace phosphor
