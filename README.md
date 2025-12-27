### Raspberry Pi project for controlling and monitoring inexpensive, unbranded Chinese diesel heaters through 433 MHz RF by using a TI CC1101 transceiver.

Library replicates the protocol used by the smaller four button remote with an OLED screen (see below), and should probably work if your heater supports this type of remote controller.

![Red remote](https://github.com/jakkik/DieselHeaterRF/blob/master/doc/red-remote.jpg?raw=true "Controllers with this type of remote are supported")

### Parts needed

* ESP32
* TI CC1101 transceiver module

Connect the SPI bus and GDO2 as follows:

![Wiring diagram](https://github.com/jakkik/DieselHeaterRF/blob/master/doc/heateresp_bb.jpg?raw=true "Wiring diagram")

    ESP32         CC1101
    -----         ------
    4   <-------> GDO2
    18  <-------> SCK
    3v3 <-------> VCC
    23  <-------> MOSI
    19  <-------> MISO
    5   <-------> CSn
    GND <-------> GND

### Features

All features of the physical remote are available through the library.

#### Get current state of the heater, including:
* Heater power state
* Current temperature setpoint
* Current pump frequency setpoint
* Ambient temperature
* Heat exchanger temperature
* Operating mode (thermostat or fixed pump frequency)
* Power supply voltage
* Current state (glowing, heating, cooling...)
* RSSI of the received signal

#### Commands
* Power on / off
* Temperature setpoint up / down (when in "auto", thermostat mode)
* Pump frequency up / down (when in "manual", fixed pump freq. mode)
* Operating mode auto / manual

#### Pairing mode
* Find the heater address

### Build instructions

```
sudo apt-get update
sudo apt-get install g++-aarch64-linux-gnu cmake ninja-build
```

Install VS Code extensions:
* “C/C++” (Microsoft).
* “CMake Tools”.

In VS Code:

    Use CMake Tools “Configure” → generates build-rpi for Pi.
    Use “Build” → produces build-rpi/diesel_heater.

On the Pi 4:

    Enable SPI: sudo raspi-config → Interface Options → SPI → Enable.

Copy binary from PC:

bash
scp build-rpi/diesel_heater pi@<pi-ip>:/home/pi/

Run:

        bash
        chmod +x diesel_heater
        ./diesel_heater

This keeps DieselHeaterRF’s protocol, CRC and CC1101 register setup exactly as in the Arduino version while making it build and run on a Pi 4 with a minimal amount of new glue code.

​

