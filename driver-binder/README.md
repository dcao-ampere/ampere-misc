# Ampere driver binder specification

Author:
* Thu Nguyen (tbnguyen@amperecomputing.com)

Created: 2021/03/30

## Problem Description
When BMC boots up with the host is Off, linux kernel will load the host
drivers, but the devices are not available so it will not bind the drivers.
Moreover, linux kernel don't support auto rebind the driver when the device
is ready. So OpenBmc have to support the application to bind/unbind the
host drivers base on the host status.

## Proposed Design

* The host driver which are setting in Json file will be binded when the CurrentHostState change to Running, Quiesced or DiagnosticMode. The SCP is ready in this case.
* When there are RequestedHostTransition or RequestedPowerTransition signal to Off, the host drivers will be unbind.

## Configuration
* Example:
```
    "hostDrivers" :
    {
            "bindDelay" : 1,
            "unbindDelay" : 0,
            "drivers" :
            [
                    {
                        "name" : "1e78a0c0.i2c-bus:smpro@4f:hwmon",
                        "path" : "/sys/bus/platform/drivers/smpro-hwmon/"
                    },
                    {
                        "name" : "1e78a0c0.i2c-bus:smpro@4e:hwmon",
                        "path" : "/sys/bus/platform/drivers/smpro-hwmon/"
                    }
            ]
    }
```

Where:

* hostDrivers: Json entry to define the host drivers.
* bindDelay: The delay will be applied before start binding the drivers in the list.
* unbindDelay: The delay will be applied before start unbinding the drivers in the list.
* drivers: Define the list of drivers in one driver type.
* name: define driver name.
* path: is the path of that driver which have bind and unbind binary.
