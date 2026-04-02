# Firmware for Environmental Sensor

This firmware is designed for a low-power environmental sensor ([KiCad project](https://github.com/Sleepyowl/microsensor-board)).

## Features

* Measures and broadcasts:

  * Temperature
  * Humidity
  * Battery voltage
  * Time until next advertisement

* Data is included in the Manufacturer Data field of BLE advertisements.

* Advertising behavior:

  * Sends 4 advertisement packets, then enters **SYSTEM_OFF** (deep sleep)
  * Advertises once per minute

* Active mode:

  * On initial power-up or button press, advertises continuously for 30 seconds
  * Allows external devices to connect during this period
  * Does not enter sleep while there are active BLE connections

* Provides a GATT service for debugging

* Supports firmware updates over BLE:

  * Use the **nRF Connect Device Manager** app

## Building

1. Install the **nRF Connect SDK**
   (Recommended: use the nRF Connect extension in Visual Studio Code)

2. Generate a signing key before building:

```bash
openssl ecparam -name prime256v1 -genkey -noout -out key.pem
```

3. Create a build (use `prj-release.conf` for release)

4. Flash the device

## Updating Firmware Over-the-Air (OTA)

1. Build the firmware
2. Locate the signed image:
   `build/microsensor/zephyr/zephyr.signed.bin`
3. Copy the image to your phone
4. Put the device in active mode by pressing the button
5. Upload the file using the **nRF Connect Device Manager** app
6. The device will automatically restart and apply the new firmware
7. If a critical error occurs, the firmware will be reverted
