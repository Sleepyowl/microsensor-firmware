# Firmware for Environmental Sensor

This firmware is designed for a low-power environmental sensor ([KiCad project](https://github.com/Sleepyowl/microsensor-board)).

It supports either ZigBee or BLE mode, selectable by user via button.

The device starts in the ZigBee mode.

## Mode selection

To select the device mode, press and hold the button for 5 seconds until LED starts fast blinking. 

Release the button. The LED will blink slower indicating the current mode of operation:
- Singular blinks mean BLE
- Double blinks mean ZigBee

Press the button to change the mode.

Otherwise wait for 5 seconds to exit the configuration mode. 

After exiting the configuration mode, the device will emit a long blink.

## ZigBee mode

The device in ZigBee mode implements sleepy end device. 

Pressing the button restarts the device.

Device exposes the following clusters and attributes (omittin mandatory clusters):
- msTemperatureMeasurement
  - measure_value
	- min_measure_value
	- max_measure_value
	- tolerance
- msRelativeHumidity
  - measure_value
	- min_measure_value
	- max_measure_value	
- genPowerCfg
  - batt_voltage_100mv
  - batt_percentage

## BLE mode

The device in BLE mode has two possible operation modes: active and low power. The device operates by default in the low power mode.

### Active mode

The active mode is activated by the button press. During the active mode, the device is connectable. 

While there's an active connection, the device stays in the active mode. Otherwise it switches back to the low power mode after 30 seconds.

This mode allows to update firmware using MCUmgr (for example with nRF Connect Device Manager app).

It also exposes several GATT services that are work in progress.

### Passive mode

The passive mode is the default mode of operation. While in this mode, the device makes exactly 4 extended announces with 100-150ms interval, and then enters System OFF. The device announces every minute.

The sensor data is send inside the announces in the Manufacturer Data. 

The payload includes:
  * Temperature
  * Humidity
  * Battery voltage
  * Time until next advertisement in ms

## Building

1. Install the **ZigBee Addon R23**
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
   - either `build/microsensor/zephyr/zephyr.signed.bin`
   - or `build/dfu_application.zip`
3. Copy the image to your phone
4. Put the device in active mode by pressing the button
5. Upload the file using the **nRF Connect Device Manager** app
6. The device will automatically restart and apply the new firmware
7. If a critical error occurs, the firmware will be reverted
