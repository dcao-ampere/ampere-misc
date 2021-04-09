/*
Please get the JEDEC file format before you read the code
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cpld.h"
#include "lattice.h"

struct cpld_dev_info *cur_dev = NULL;
struct cpld_dev_info **cpld_dev_list;

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

const char *cpld_list[] = {
  "LCMXO2-2000HC",
  "LCMXO2-4000HC",
  "LCMXO2-7000HC",
  "MAX10-10M16",
  "MAX10-10M25",
};

static int cpld_probe(cpld_intf_t intf, uint8_t id, void *attr)
{
  if (cur_dev == NULL)
    return -1;

  if (!cur_dev->cpld_open) {
    printf("Open not supported\n");
    return -1;
  }

  return cur_dev->cpld_open(intf, id, attr);
}

static int cpld_remove(cpld_intf_t intf)
{
  if (cur_dev == NULL)
    return -1;

  if (!cur_dev->cpld_close) {
    printf("Close not supported\n");
    return -1;
  }

  return cur_dev->cpld_close(intf);
}

static int cpld_malloc_list()
{
  unsigned int i, j = 0;
  unsigned int dev_cnts = 0;

  dev_cnts += ARRAY_SIZE(lattice_dev_list);

  cpld_dev_list = (struct cpld_dev_info **)malloc(sizeof(struct cpld_dev_info *) * dev_cnts);

  for (i = 0; i < ARRAY_SIZE(lattice_dev_list); i++, j++)
    cpld_dev_list[j] = &lattice_dev_list[i];

  return dev_cnts;
}

int cpld_intf_open(uint8_t cpld_index, cpld_intf_t intf, void *attr)
{
  int i;
  int dev_cnts;

  if (cur_dev != NULL)
    return 0;

  if (cpld_index >= UNKNOWN_DEV) {
    printf("Unknown CPLD index = %d\n", cpld_index);
    return -1;
  }

  dev_cnts = cpld_malloc_list();
  for (i = 0; i < dev_cnts; i++) {
    if (!strcmp(cpld_list[cpld_index], cpld_dev_list[i]->name)) {
      cur_dev = cpld_dev_list[i];
      break;
    }
  }
  if (i == dev_cnts) {
    //No CPLD device match
    printf("Unknown CPLD name = %s\n", cpld_list[cpld_index]);
    free(cpld_dev_list);
    return -1;
  }

  return cpld_probe(intf, cpld_index, attr);
}

int cpld_intf_close(cpld_intf_t intf)
{
  int ret;

  ret = cpld_remove(intf);
  free(cpld_dev_list);
  cur_dev = NULL;

  return ret;
}

int cpld_get_ver(uint32_t *ver)
{
  if (cur_dev == NULL)
    return -1;

  if (!cur_dev->cpld_ver) {
    printf("Get version not supported\n");
    return -1;
  }

  return cur_dev->cpld_ver(ver);
}

int cpld_get_device_id(uint32_t *dev_id)
{
  if (cur_dev == NULL)
    return -1;

  if (!cur_dev->cpld_dev_id) {
    printf("Get device id not supported\n");
    return -1;
  }

  return cur_dev->cpld_dev_id(dev_id);
}

int cpld_erase(void)
{
  if (cur_dev == NULL)
    return -1;

  if (!cur_dev->cpld_erase) {
    printf("Erase CPLD not supported\n");
    return -1;
  }

  return cur_dev->cpld_erase();
}

int cpld_program(char *file, char *key, char is_signed)
{
  int ret;
  FILE *fp_in = NULL;

  if (cur_dev == NULL)
    return -1;

  if (!cur_dev->cpld_program) {
    printf("Program CPLD not supported\n");
    return -1;
  }

  fp_in = fopen(file, "r");
  if (NULL == fp_in) {
    printf("[%s] Cannot Open File %s!\n", __func__, file);
    return -1;
  }

  ret = cur_dev->cpld_program(fp_in, key, is_signed);
  fclose(fp_in);

  return ret;
}

int cpld_verify(char *file)
{
  int ret;
  FILE *fp_in = NULL;

  if (cur_dev == NULL)
    return -1;

  if (!cur_dev->cpld_verify) {
    printf("Verify CPLD not supported\n");
    return -1;
  }

  fp_in = fopen(file, "r");
  if (NULL == fp_in) {
    printf("[%s] Cannot Open File %s!\n", __func__, file);
    return -1;
  }

  cur_dev->cpld_verify(fp_in);
  fclose(fp_in);

  return ret;
}
