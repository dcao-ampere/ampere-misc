/*
* main - CPLD upgrade utility via BMC's JTAG master
*/

#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#include <termios.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <pthread.h>
#include "cpld.h"

typedef struct {
  int program;                    /* enable/disable program  */
  int erase;                      /* enable/disable erase flag */
  int get_version;                /* get cpld version flag */
  int get_device;                 /* get cpld ID code flag */
}cpld_t;

static void usage(FILE *fp, char **argv)
{
  fprintf(fp,
          "Usage: %s [options]\n\n"
          "Options:\n"
          " -h | --help                   Print this message\n"
          " -p | --program                Erase, program and verify cpld\n"
          " -v | --get-cpld-version       Get current cpld version\n"
          " -i | --get-cpld-idcode        Get cpld idcode\n"
          "",
          argv[0]);
}

static const char short_options [] = "hvip:";

static const struct option
  long_options [] = {
  { "help",                 no_argument,        NULL,    'h' },
  { "program",              required_argument,  NULL,    'p' },
  { "get-cpld-version",     no_argument,        NULL,    'v' },
  { "get-cpld-idcode",      no_argument,        NULL,    'i' },
  { 0, 0, 0, 0 }
};

static void printf_pass()
{
  printf("+=======+\n" );
  printf("| PASS! |\n" );
  printf("+=======+\n\n" );
}

static void printf_failure()
{
  printf("+=======+\n" );
  printf("| FAIL! |\n" );
  printf("+=======+\n\n" );
}

int main(int argc, char *argv[]){
  char option;
  char in_name[100]  = "";
  uint8_t cpld_var[4] = {0};
  char key[32]= {0};
  cpld_t cpld;
  int rc = -1;

  printf( "\n******************************************************************\n" );
  printf( "                 Ampere Computing.\n" );
  printf( "             ampere_cpld_fwupdate-v0.0.1 Copyright 2021.\n");
  printf( "******************************************************************\n\n" );

  memset(&cpld, 0, sizeof(cpld));

  while ((option = getopt_long(argc, argv, short_options, long_options, NULL)) != (char) -1) {
      switch (option) {
      case 'h':
          usage(stdout, argv);
          exit(EXIT_SUCCESS);
          break;
      case 'p':
          cpld.program = 1;
          strcpy(in_name, optarg);
          if (!strcmp(in_name, "")) {
              printf("No input file name!\n");
              usage(stdout, argv);
              exit(EXIT_FAILURE);
          }
          break;
      case 'v':
          cpld.get_version = 1;
          break;
      case 'i':
          cpld.get_device = 1;
          break;
      default:
          usage(stdout, argv);
          exit(EXIT_FAILURE);
      }
  }

  if (cpld_intf_open(LCMXO2_4000HC, INTF_JTAG, NULL)) {
    printf("CPLD_INTF Open failed!\n");
    exit(EXIT_FAILURE);
  }

  if (cpld.get_version) {
    if (cpld_get_ver((unsigned int *)&cpld_var)) {
      printf("CPLD Version: NA\n");
    } else {
      printf("CPLD Version: %02X%02X%02X%02X\n", cpld_var[3], cpld_var[2],
              cpld_var[1], cpld_var[0]);
    }
    exit(EXIT_SUCCESS);
  }

  if (cpld.get_device) {
    if (cpld_get_device_id((unsigned int *)&cpld_var)) {
      printf("CPLD DeviceID: NA\n");
    } else {
      printf("CPLD DeviceID: %02X%02X%02X%02X\n", cpld_var[3], cpld_var[2],
              cpld_var[1], cpld_var[0]);
    }
    exit(EXIT_SUCCESS);
  }

  if (cpld.program) {
    // Print CPLD Version
    if (cpld_get_ver((unsigned int *)&cpld_var)) {
      printf("CPLD Version: NA\n");
    } else {
      printf("CPLD Version: %02X%02X%02X%02X\n", cpld_var[3], cpld_var[2],
              cpld_var[1], cpld_var[0]);
    }
    // Print CPLD Device ID
    if (cpld_get_device_id((unsigned int *)&cpld_var)) {
      printf("CPLD DeviceID: NA\n");
    } else {
      printf("CPLD DeviceID: %02X%02X%02X%02X\n", cpld_var[3], cpld_var[2],
              cpld_var[1], cpld_var[0]);
    }
    rc = cpld_program(in_name, key, 0);
    if(rc < 0) {
        printf("Failed to program cpld\n");
        goto end_of_func;
    }
  }

end_of_func:
  cpld_intf_close(INTF_JTAG);
  if(rc == 0) {
      printf_pass();
      return 0;
  }else{
      printf_failure();
      return -1;
  }
}
