#include "smbus.hpp"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <iostream>
#include <mutex>

#include "i2c.h"

#define MAX_I2C_BUS 30
#define MAX_READ_RETRY 10

static constexpr bool DEBUG = false;

static int fd[MAX_I2C_BUS] = {0};

namespace phosphor
{
namespace smbus
{

std::mutex gMutex;

int phosphor::smbus::Smbus::openI2cDev(int i2cbus, char* filename, size_t size,
                                       int quiet)
{
    int file;

    snprintf(filename, size, "/dev/i2c/%d", i2cbus);
    filename[size - 1] = '\0';
    file = open(filename, O_RDWR);

    if (file < 0 && (errno == ENOENT || errno == ENOTDIR))
    {
        sprintf(filename, "/dev/i2c-%d", i2cbus);
        file = open(filename, O_RDWR);
    }

    if (file < 0)
    {
        if (errno == ENOENT)
        {
            fprintf(stderr,
                    "Error: Could not open file "
                    "`/dev/i2c-%d' or `/dev/i2c/%d': %s\n",
                    i2cbus, i2cbus, strerror(ENOENT));
        }
        else
        {
            fprintf(stderr,
                    "Error: Could not open file "
                    "`%s': %s\n",
                    filename, strerror(errno));
            if (errno == EACCES)
                fprintf(stderr, "Run as root?\n");
        }
    }

    return file;
}

int phosphor::smbus::Smbus::smbusInit(int smbus_num)
{
    int res = 0;
    char filename[20];

    gMutex.lock();

    fd[smbus_num] = openI2cDev(smbus_num, filename, sizeof(filename), 0);
    if (fd[smbus_num] < 0)
    {
        gMutex.unlock();

        return -1;
    }

    res = fd[smbus_num];

    gMutex.unlock();

    return res;
}

int phosphor::smbus::Smbus::smbusMuxToChan(int smbus_num, int8_t addr,
                                           uint8_t chan)
{
    int ret = 0;

    gMutex.lock();
    ret = i2c_set_address(fd[smbus_num], addr);

    if (ret < 0)
    {
        gMutex.unlock();
        return ret;
    }

    ret = i2c_smbus_write_byte(fd[smbus_num], chan);
    if (ret < 0)
    {
        gMutex.unlock();
        return ret;
    }

    gMutex.unlock();
    return ret;
}

uint8_t phosphor::smbus::Smbus::smbusReadByteData(int smbus_num, int8_t addr,
                                                  uint8_t offset)
{
    int ret = 0;
    uint8_t value = 0;

    ret = i2c_set_address(fd[smbus_num], addr);
    if (ret < 0)
    {
        fprintf(stderr, "Error: set the address failed\n");
        return ret;
    }

    value = i2c_smbus_read_byte_data(fd[smbus_num], offset);

    return value;
}

int32_t phosphor::smbus::Smbus::smbusReadWordData(int smbus_num, int8_t addr,
                                                  uint8_t offset)
{
    int ret = 0;
    int32_t value = 0;

    ret = i2c_set_address(fd[smbus_num], addr);
    if (ret < 0)
    {
        fprintf(stderr, "Error: set the address failed\n");
        return ret;
    }

    value = i2c_smbus_read_word_data(fd[smbus_num], offset);

    return value;
}

void phosphor::smbus::Smbus::smbusClose(int smbus_num)
{
    close(fd[smbus_num]);
}

} // namespace smbus
} // namespace phosphor
