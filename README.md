# My scratch pad for experiments with the TTGO HiGrow

## Useful links
* Vendor firmware: https://github.com/Xinyuan-LilyGO/LilyGo-HiGrow/
* Home assistant integration discussion: https://community.home-assistant.io/t/ttgo-higrow-with-esphome/144053

## ESPHome integration

[`plantsensor.yaml`](https://github.com/jowiho/plantsensor/blob/main/plantsensor.yaml)
is a [ESPHome](https://esphome.io/) configuration file that contains all sensors.

To reduce energy consumption when running off of a battery, the config enables deep sleep cycles of 4 hours.
After every cycle it wakes up and takes 1 or 2 measurements for each sensor.
Press the Wake button on the side of the device to interrupt the sleep cycle and take an immediate measurement.

Note: you should temporarily disable deep sleep before trying to add the ESPHome device to your Home Assistant.

## Notes
* Power consumption in deep sleep is 103 uA when powered by a battery (4 V, 124 uW). In theory that should be enough to run 80 days on a 200 mAh LiPo cell, disregarding increased power consumption while awake.
* Power consumption in deep sleep is 8 mA when powered over USB (5 V, 40 mW).
* Temperature sensor always seems to return a too high value. Possibly picking up heat from the board?
* BME280 altitude is useless for a static plant sensor
* Salt sensor is pretty useless
