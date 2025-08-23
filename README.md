# Water Level Sensor
This project uses an ESP32 on battery to measure the water level in a tank an reports it to a MQTT server over Wifi. It is meant to work on battery but can be used on USB power.

## Hardware
 * [Lilygo T-OI](https://lilygo.cc/products/t-oi-plus) (it should work with other ESP32-based boards but it has not been tested)
 * Ultra Sonic module AJ-SR04M (for example [here](https://www.aliexpress.com/item/33002362860.html?spm=a2g0o.order_list.0.0.75981802fHvoEg)). It won't work with any other variant like AJ-SR04M-2

 Optional:
  * A 16340 battery
  * Wifi antenna

The sensor should be placed in the tank where it will not pick up a reading from other obstacle so mind the [reading radius](https://github.com/tomaskovacik/kicad-library/blob/master/library/datasheet/K02-AJ-SR04/AJ-SR04M-T-X.zh-CN.en.pdf) 

## Preparation and building
In this application, the distance sensor is used in [Mode 2](https://www.mantech.co.za/Datasheets/Products/AJ-SR04M-200925A.pdf) (Low Power Consumption Mode), so a 330 kΩ resistor must be soldered on R19.

Rename config.cpp.sample to config.h and adapt the settings to your environment.

 ## Software configuration
 The software is configured over MQTT. You need to send a JSON message to the device on topic **ROOT_TOPIC/config** with configuration values. The possible settings are (case-sensitive):
  * *minValue* (Default **200**mm): the value read when there is no water in the tank. The sensor has a blind area of 20cm so the default value is as low has it can get. This ensures that the correct value is detected as the level in the tank changes. 
  * *maxValue* (Default **8000**mm): the value read when the tank is full. The sensor cannot measure distamces of more than 8m.This ensures that the correct value is detected as the level in the tnak changes. 
  * *sleepTime* (Default **300**s): the time between 2 readings in seconds. This setting has a huge impact on autonomy.
  * *maxDifference* (Default **200**mm): the maximum difference allowed between 2 readings. If the difference is higher, another reading is performed.
  * *logLevel* (Default **3**): a value [between 0 and 6](https://github.com/thijse/Arduino-Log) to define how much is logged.

Example:
  ```json
  {
    "minLevel": 400,
    "maxDifference": 15
  }
  ```

If you configure more than one probe, you can use indexed minLevel and maxLevel:
  ```json
  {
    "minLevel[0]": 1000,
    "maxLevel[0]": 300,
    "maxLevel[1]": 250
  }
  ```

 **Mind the quotes, punctuation and casing!**

Make sure you send the config with the **Retain** option. The values are read at the end of the reading cycle so it will take up to 5 minutes for the settings to apply. To speed up the process, you can push the reset button to trigger a new cycle.

### Getting log files
It is possible to get log files from previous run. Send the file name on **ROOT_TOPIC/file/get** (e.g. "/log001.txt"). The content is sent on the **ROOT_TOPIC/file/data** topic. You can also get a list of all the files by sending a folder name (typically "/") on **ROOT_TOPIC/file/dirlist**. The result is sent on **ROOT_TOPIC/file/dir/FOLDER_NAME** (i.e. if you requested the listing for the root folder, the answer would come on **ROOT_TOPIC/file/dir/**).

### Remote update (not working)
You can update the firmware remotely by sending the url of the firmware on topic **ROOT_TOPIC/update/url**. Only works in http port 80 or using TFTP. On Linux, you can easily start a TFTP server using:
```bash
dnsmasq --port=0 --enable-tftp --tftp-root=/tmp --tftp-no-blocksize --user=root --group=root
```

## Hardware setup
This is how you connect your ESP32:
![Probe connections](Probe%20connections.drawio.png "Probe connections")

![Sensor installation](Sensor%20installation.drawio.png "Sensor installation")
