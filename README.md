# Firmware for Environmental Sensor

Firmware for a low-power environmental sensor (see https://github.com/BeeEyeIoT/microsensor-board).

The device supports two wireless modes: ZigBee and BLE, selectable via a button.  
By default, the device starts in ZigBee mode.

## Mode Selection

To enter mode selection:

1. Press and hold the button for 5 seconds until the LED starts fast blinking.  
2. Release the button.

The LED will then blink slowly to indicate the current mode:
- Single blink → BLE  
- Double blink → ZigBee  

Press the button to switch modes.

If no action is taken for 5 seconds, the device exits configuration mode.  
Upon exit, the LED performs a long blink.

## ZigBee Mode

In ZigBee mode, the device operates as a sleepy end device.

- Pressing the button restarts the device.

### Exposed Clusters and Attributes  
(Mandatory clusters omitted)

- msTemperatureMeasurement
  - measured_value  
  - min_measured_value  
  - max_measured_value  
  - tolerance  

- msRelativeHumidity
  - measured_value  
  - min_measured_value  
  - max_measured_value  

- genPowerCfg
  - battery_voltage_100mV  
  - battery_percentage  

## BLE Mode

BLE mode has two operating states:
- Low Power (default)
- Active

### Active Mode

Activated by a button press and also enabled on the first boot (after reset or power-on).

- Device becomes connectable
- Remains active while a connection is established
- If not connected, returns to low power after 30 seconds

Supports:
- Firmware updates via MCUmgr (e.g. using nRF Connect Device Manager)
- Experimental GATT services (work in progress)

### Low Power Mode

(Default operating state after the active period)

- Device sends 4 extended advertisements at 100–150 ms intervals
- Then enters System OFF
- Repeats every 60 seconds

Sensor data is included in Manufacturer Data:

- Temperature  
- Humidity  
- Battery voltage  
- Time until next advertisement (ms)

## Building

1. Install ZigBee Addon R23  
   (Recommended: use the nRF Connect for VS Code extension)

2. Generate a signing key:

   openssl ecparam -name prime256v1 -genkey -noout -out key.pem

3. Build the firmware  
   - Use `prj-release.conf` for release builds

4. Flash the device

## OTA Firmware Update

1. Build the firmware  
2. Locate the signed image:
   - build/microsensor/zephyr/zephyr.signed.bin  
   - or build/dfu_application.zip  
3. Copy the file to your phone  
4. Enter Active Mode (press the button or use initial boot window)  
5. Upload using nRF Connect Device Manager  
6. Device will reboot and apply the update  
7. On critical failure, the firmware is automatically reverted  