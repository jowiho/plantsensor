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
* Power consumption during deep sleep is 40 mW; too much for a small 200 mAh battery
* Temperature sensor always seems to return a too high value. Possibly picking up heat from the board?
* BME280 altitude is useless for a static plant sensor
* Salt sensor is pretty useless
