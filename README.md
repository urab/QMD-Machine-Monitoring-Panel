# QMD Machine Monitoring Panel
Industrial machine monitoring system for injection molding machines.

## Network Configuration
Default Ethernet configuration:

| Parameter | Value |
|------------|----------------|
| IP Address | 192.168.10.250 |
| Gateway | 192.168.10.1 |
| DNS Server | 192.168.10.1 |
| Subnet Mask | 255.255.255.0 |

### Web Interface
Open the main dashboard in your web browser:

http://192.168.10.250
Open the settings page:
http://192.168.10.250/setting

### Features
- Real-time monitoring of 12 injection molding machines
- Live machine status indication
- Alarm management with audible notification
- Mute (ACK) button with automatic alarm reactivation
- Water temperature monitoring
- Built-in thermostat control
- Real-Time Clock (DS3231)
- Ethernet Web Dashboard
- REST API support

## Required Libraries
Install the following libraries using the Arduino Library Manager:

- SPI
- Ethernet
- OneWire
- DallasTemperature
- RTClib (by Adafruit)
The **SPI** and **Ethernet** libraries are included with the Arduino IDE.

## Hardware
- Arduino Mega 2560
- Ethernet Shield W5100
- DS18B20 Temperature Sensor
- DS3231 RTC Module
- 12 Machine Status Inputs
- Alarm Relay
- Thermostat Relay
- ACK / Mute Push Button

## License
This project was developed by **Yuriy Bant** for industrial monitoring of injection molding machines.
© 2026 All rights reserved.

## Pin Assignment
### Main Outputs and Sensors

| Function | Arduino Mega Pin | Signal Type |
|---|---:|---|
| Alarm siren relay | D2 | Output, active LOW |
| DS18B20 temperature sensor | D6 | OneWire data |
| Mute push button | D7 | Input with INPUT_PULLUP |
| Thermostat relay | D9 | Output, active HIGH |
| DS3231 RTC SDA | D20 | I2C SDA |
| DS3231 RTC SCL | D21 | I2C SCL |
| Ethernet W5100 CS | D10 | SPI chip select |
| SD card CS disabled | D4 | Kept HIGH |

### Machine Input Mapping
Each machine uses three isolated dry-contact inputs:

- Blue — machine stopped / standby
- Green — machine running
- Red — machine alarm

All inputs use `INPUT_PULLUP`.
Active signal means the input is connected to GND.

| Machine | Blue | Green | Red |
|---|---:|---:|---:|
| 45 | D22 | D23 | D24 |
| 50 | D25 | D26 | D27 |
| 51 | D28 | D29 | D30 |
| 60 | D31 | D32 | D33 |
| 120 | D34 | D35 | D36 |
| 110 | D37 | D38 | D39 |
| 121 | D40 | D41 | D42 |
| 61 | D43 | D44 | D45 |
| 200 | D46 | D47 | D48 |
| 380 | D49 | A0 / D54 | A1 / D55 |
| 90 | A2 / D56 | A3 / D57 | A4 / D58 |
| 122 | A5 / D59 | A6 / D60 | A7 / D61 |

## Electrical Connection
Machine signals must not be connected directly to the Arduino.
Use galvanic isolation between the industrial machine signals and the Arduino inputs.

Recommended connection:
```text
Machine signal / dry contact
        ↓
Optocoupler or isolation relay
        ↓
Arduino digital input
        ↓
INPUT_PULLUP

## Screenshots
(Project photos will be added.)

## Documentation
 [System Architecture](SYSTEM_ARCHITECTURE.md)
 [Project Photos](Photos/README.md)

## Author
Designed and developed by
Ura Bant
