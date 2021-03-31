#ifndef __CPLD_H__
#define __CPLD_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef enum {
  INTF_I2C,
  INTF_JTAG
} cpld_intf_t;

typedef struct {
  uint8_t bus_id;
  uint8_t slv_addr;
  uint8_t img_type;
  uint32_t start_addr;
  uint32_t end_addr;
  uint32_t csr_base;
  uint32_t data_base;
  uint32_t boot_base;
} altera_max10_attr_t;

int cpld_intf_open(uint8_t cpld_index, cpld_intf_t intf, void *attr);
int cpld_intf_close(cpld_intf_t intf);
int cpld_get_ver(uint32_t *ver);
int cpld_get_device_id(uint32_t *dev_id);
int cpld_erase(void);
int cpld_program(char *file, char* key, char is_signed);
int cpld_verify(char *file);

struct cpld_dev_info {
  const char *name;
  uint32_t dev_id;
  int (*cpld_open)(cpld_intf_t intf, uint8_t id, void *attr);
  int (*cpld_close)(cpld_intf_t intf);
  int (*cpld_ver)(uint32_t *ver);
  int (*cpld_erase)(void);
  int (*cpld_program)(FILE *fd, char *key, char is_signed);
  int (*cpld_verify)(FILE *fd);
  int (*cpld_dev_id)(uint32_t *dev_id);
};

enum {
  LCMXO2_2000HC = 0,
  LCMXO2_4000HC,
  LCMXO2_7000HC,
  MAX10_10M16,
  MAX10_10M25,
  UNKNOWN_DEV
};

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* __CPLD_H__ */
