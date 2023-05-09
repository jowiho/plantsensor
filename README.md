# My scratch pad for experiments with the TTGO HiGrow

## Useful links
* Vendor firmware: https://github.com/Xinyuan-LilyGO/LilyGo-HiGrow/
* Home assistant integration discussion: https://community.home-assistant.io/t/ttgo-higrow-with-esphome/144053

## ESPHome integration

[`plantsensor.yaml`]()https://github.com/jowiho/plantsensor/blob/main/plantsensor.yaml)
is a [ESPHome](https://esphome.io/) configuration file that enables all
sensors. So save energy consumption when running off of a battery, it places
uses deep sleep cycles of 1 minute. After every cycle it wakes up and takes 1
or 2 measurements for each sensor. For production use you'll want to increase
the deep sleep cycle to a couple of hours at least.

## Notes
* Temperature sensor always seems to return a too high value. Possibly picking up heat from the board?
* BME280 altitude is useless for a static plant sensor
* Salt sensor is pretty useless
