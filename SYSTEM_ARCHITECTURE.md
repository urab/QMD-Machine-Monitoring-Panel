# System Architecture

```text
                    +----------------------+
                    |   Injection Machines |
                    +----------+-----------+
                               |
                               |
                     Dry contacts / Optocouplers
                               |
                               v
                    +----------------------+
                    |   Arduino Mega 2560  |
                    |  Ethernet W5100      |
                    +----------+-----------+
                               |
                     Ethernet LAN
                               |
                               v
                    +----------------------+
                    |  Web Dashboard       |
                    +----------+-----------+
                               |
                               v
                    +----------------------+
                    | Plant Operator       |
                    +----------------------+

Temperature
   |
   +--> DS18B20

Clock
   |
   +--> DS3231 RTC

Outputs
   |
   +--> Alarm Relay
   |
   +--> Thermostat Relay
```
