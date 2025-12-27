// src/main.cpp
#include <iostream>
#include "DieselHeaterRF.h"
#include "pi_arduino_compat.h"
#include "pi_gpio.h"
#include "pi_spi.h"

// Define the global SPI instance.
PiSPI g_spi("/dev/spidev0.0", 4000000);

int main() {
    DieselHeaterRF heater;  // uses default pin constants from header

    heater.begin();

    std::cout << "Pairing for 60 seconds; put heater/remote into pairing.\n";
    uint32_t start = millis();
    uint32_t addr  = 0;
    while (millis() - start < 60000 && addr == 0) {
        addr = heater.findAddress(500);
        delay(100);
    }

    if (addr == 0) {
        std::cout << "No address learned.\n";
        return 1;
    }

    std::cout << "Learned address: 0x" << std::hex << addr << std::dec << "\n";
    heater.setAddress(addr);

    std::cout << "Sending POWER command...\n";
    heater.sendCommand(HEATER_CMD_POWER);

    heater_state_t st{};
    if (heater.getState(&st)) {
        std::cout << "Power=" << int(st.power)
                  << " setpoint=" << int(st.setpoint)
                  << " ambient=" << int(st.ambientTemp) << "\n";
    } else {
        std::cout << "No state packet received.\n";
    }

    return 0;
}
