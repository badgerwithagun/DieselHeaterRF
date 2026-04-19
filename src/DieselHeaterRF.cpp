/*
 * DieselHeaterRF.cpp
 * Copyright (c) 2020 Jarno Kyttälä
 *
 * Simple class to control an inexpensive Chinese diesel heater through
 * 433 MHz RF using a TI CC1101 transceiver.  Replicates the protocol
 * used by the four-button "red LCD remote" with an OLED screen.
 */

#include "DieselHeaterRF.h"

void DieselHeaterRF::begin() {
  begin(0);
}

void DieselHeaterRF::begin(uint32_t heaterAddr) {

  _heaterAddr = heaterAddr;

  pinModePi(_pinSck,  PI_OUTPUT);
  pinModePi(_pinMosi, PI_OUTPUT);
  pinModePi(_pinMiso, PI_INPUT);
  pinModePi(_pinSs,   PI_OUTPUT);
  pinModePi(_pinGdo2, PI_INPUT);

  // No SPI.begin on Pi; pi_spi.h sets up /dev/spidev0.0 globally.

  delay(100);

  initRadio();

}

void DieselHeaterRF::setAddress(uint32_t heaterAddr) {
  _heaterAddr = heaterAddr;
}

bool DieselHeaterRF::getState(heater_state_t *state) {
  return getState(state, HEATER_RX_TIMEOUT);
}

bool DieselHeaterRF::getState(heater_state_t *state, uint32_t timeout) {
  return getState(&state->state, &state->power, &state->voltage, &state->ambientTemp, &state->caseTemp, &state->setpoint, &state->pumpFreq, &state->autoMode, &state->rssi, timeout);
}

bool DieselHeaterRF::getState(uint8_t *state, uint8_t *power, float *voltage, int8_t *ambientTemp, uint8_t *caseTemp, int8_t *setpoint, float *pumpFreq, uint8_t *autoMode, int16_t *rssi, uint32_t timeout) {

  char buf[24];

  if (receivePacket(buf, timeout)) {

    uint32_t address = parseAddress(buf);

    if (address != _heaterAddr) return false;

    *state = buf[6];
    *power = buf[7];
    *voltage = buf[9] / 10.0f;
    *ambientTemp = buf[10];
    *caseTemp = buf[12];
    *setpoint = buf[13];
    *autoMode = buf[14] == 0x32; // 0x32 = auto (thermostat), 0xCD = manual (Hertz mode) 
    *pumpFreq = buf[15] / 10.0f;
    *rssi = (buf[22] - (buf[22] >= 128 ? 256 : 0)) / 2 - 74;
    return true;
  }

  return false;

}

void DieselHeaterRF::sendCommand(uint8_t cmd) {
  if (_heaterAddr == 0x00) return;
  sendCommand(cmd, _heaterAddr, HEATER_TX_REPEAT);
}

void DieselHeaterRF::sendCommand(uint8_t cmd, uint32_t addr) {
  sendCommand(cmd, addr, HEATER_TX_REPEAT);
}

void DieselHeaterRF::sendCommand(uint8_t cmd, uint32_t addr, uint8_t numTransmits) {

  unsigned long t;
  char buf[10];

  buf[0] = 9; // Packet length, excl. self
  buf[1] = cmd;
  buf[2] = (addr >> 24) & 0xFF;
  buf[3] = (addr >> 16) & 0xFF;
  buf[4] = (addr >> 8) & 0xFF;
  buf[5] = addr & 0xFF;
  buf[6] = _packetSeq++;
  buf[9] = 0;

  uint16_t crc = crc16_2(buf, 7);

  buf[7] = (crc >> 8) & 0xFF;
  buf[8] = crc & 0xFF;

  for (int i = 0; i < numTransmits; i++) {
    txBurst(10, buf);
    t = millis();
    while (readStatusReg(0x35) != 0x01) { delay(1); if (millis() - t > 100) { return; } } // Wait for idle state
  }

}

uint32_t DieselHeaterRF::findAddress(uint16_t timeout) {

  char buf[24];

  if (receivePacket(buf, timeout)) {
    uint32_t address = parseAddress(buf);
    return address;
  }

  return 0;

}

uint32_t DieselHeaterRF::parseAddress(char *buf) {
  uint32_t address = 0;
  address |= (uint32_t(buf[2]) << 24);
  address |= (uint32_t(buf[3]) << 16);
  address |= (uint32_t(buf[4]) << 8);
  address |= uint8_t(buf[5]);
  return address;
}

bool DieselHeaterRF::receivePacket(char *bytes, uint16_t timeout) {

  unsigned long t = millis();
  uint8_t rxLen;

  rxFlush();
  rxEnable();

  while (1) {

    if (millis() - t > timeout) return false;

    // Wait for GDO2 assertion
    while (!digitalReadPi(_pinGdo2)) {
      if (millis() - t > timeout) return false;
    }

    // Get number of bytes in RX FIFO
    rxLen = readStatusReg(0x3B); // RXBYTES
    
    if (rxLen == 24) break;

    // Flush RX FIFO
    rxFlush();
    rxEnable();
    
  }

  // Read RX FIFO
  rx(rxLen, bytes);
  rxFlush();

  uint16_t crc = crc16_2(bytes, 19);
  if (crc == (uint16_t(bytes[19]) << 8) + uint8_t(bytes[20])) {
    return true;
  }

  return false;
  
}

void DieselHeaterRF::initRadio() {

  strobe(0x30); // SRES

  delay(100);

  writeConfigReg(0x00, 0x07); // IOCFG2
  writeConfigReg(0x02, 0x06); // IOCFG0
  writeConfigReg(0x03, 0x47); // FIFOTHR
  writeConfigReg(0x07, 0x04); // PKTCTRL1
  writeConfigReg(0x08, 0x05); // PKTCTRL0
  writeConfigReg(0x0A, 0x00); // CHANNR
  writeConfigReg(0x0B, 0x06); // FSCTRL1
  writeConfigReg(0x0C, 0x00); // FSCTRL0
  writeConfigReg(0x0D, 0x10); // FREQ2
  writeConfigReg(0x0E, 0xB1); // FREQ1
  writeConfigReg(0x0F, 0x3B); // FREQ0
  writeConfigReg(0x10, 0xF8); // MDMCFG4
  writeConfigReg(0x11, 0x93); // MDMCFG3
  writeConfigReg(0x12, 0x13); // MDMCFG2
  writeConfigReg(0x13, 0x22); // MDMCFG1
  writeConfigReg(0x14, 0xF8); // MDMCFG0
  writeConfigReg(0x15, 0x26); // DEVIATN
  writeConfigReg(0x17, 0x30); // MCSM1
  writeConfigReg(0x18, 0x18); // MCSM0
  writeConfigReg(0x19, 0x16); // FOCCFG
  writeConfigReg(0x1A, 0x6C); // BSCFG
  writeConfigReg(0x1B, 0x03); // AGCTRL2
  writeConfigReg(0x1C, 0x40); // AGCTRL1
  writeConfigReg(0x1D, 0x91); // AGCTRL0
  writeConfigReg(0x20, 0xFB); // WORCTRL
  writeConfigReg(0x21, 0x56); // FREND1
  writeConfigReg(0x22, 0x17); // FREND0
  writeConfigReg(0x23, 0xE9); // FSCAL3
  writeConfigReg(0x24, 0x2A); // FSCAL2
  writeConfigReg(0x25, 0x00); // FSCAL1
  writeConfigReg(0x26, 0x1F); // FSCAL0
  writeConfigReg(0x2C, 0x81); // TEST2
  writeConfigReg(0x2D, 0x35); // TEST1
  writeConfigReg(0x2E, 0x09); // TEST0
  writeConfigReg(0x09, 0x00); // ADDR
  writeConfigReg(0x04, 0x7E); // SYNC1
  writeConfigReg(0x05, 0x3C); // SYNC0

  static const uint8_t patable[8] = {0x00, 0x12, 0x0E, 0x34, 0x60, 0xC5, 0xC1, 0xC0};
  writeBurstReg(0x3E, patable, 8); // PATABLE

  strobe(0x31); // SFSTXON
  strobe(0x36); // SIDLE
  strobe(0x3B); // SFTX
  strobe(0x36); // SIDLE
  strobe(0x3A); // SFRX

  delay(136);

}

void DieselHeaterRF::txBurst(uint8_t len, char *bytes) {
    txFlush();
    writeBurstReg(0x3F, reinterpret_cast<const uint8_t *>(bytes), len); // TXFIFO burst write
    strobe(0x35); // STX
}

void DieselHeaterRF::txFlush() {
    strobe(0x36); // SIDLE
    strobe(0x3B); // SFTX
    delay(16); // Prevent TX underflow when bursting immediately after flush
}

void DieselHeaterRF::rx(uint8_t len, char *bytes) {
    for (int i = 0; i < (int)len; i++)
        bytes[i] = static_cast<char>(readConfigReg(0x3F)); // RXFIFO single read
}

void DieselHeaterRF::rxFlush() {
    strobe(0x36); // SIDLE
    (void)readConfigReg(0x3F); // Dummy RXFIFO read to de-assert GDO2
    strobe(0x3A); // SFRX
    delay(16);
}

void DieselHeaterRF::rxEnable() {
    strobe(0x34); // SRX
}

// ---------------------------------------------------------------------------
// CC1101 SPI primitives
// ---------------------------------------------------------------------------

void DieselHeaterRF::spiTransaction(const uint8_t *tx, uint8_t *rx, size_t len) {
    digitalWritePi(_pinSs, PI_LOW);
    while (digitalReadPi(_pinMiso)) {} // Wait for CHIP_RDYn
    g_spi.transfer_buf(tx, rx, len);
    digitalWritePi(_pinSs, PI_HIGH);
}

void DieselHeaterRF::writeConfigReg(uint8_t addr, uint8_t val) {
    uint8_t tx[2] = { static_cast<uint8_t>(addr & 0x3F), val };
    uint8_t rx[2];
    spiTransaction(tx, rx, 2);
}

uint8_t DieselHeaterRF::readConfigReg(uint8_t addr) {
    uint8_t tx[2] = { static_cast<uint8_t>(0x80 | (addr & 0x3F)), 0x00 };
    uint8_t rx[2] = {};
    spiTransaction(tx, rx, 2);
    return rx[1];
}

uint8_t DieselHeaterRF::readStatusReg(uint8_t addr) {
    // Status registers require the burst bit set (0xC0) even for single reads.
    uint8_t tx[2] = { static_cast<uint8_t>(0xC0 | (addr & 0x3F)), 0x00 };
    uint8_t rx[2] = {};
    spiTransaction(tx, rx, 2);
    return rx[1];
}

void DieselHeaterRF::strobe(uint8_t cmd) {
    uint8_t tx[1] = { cmd };
    uint8_t rx[1];
    spiTransaction(tx, rx, 1);
}

void DieselHeaterRF::writeBurstReg(uint8_t addr, const uint8_t *data, uint8_t len) {
    // Max burst in this application: TXFIFO (10 bytes) or PATABLE (8 bytes).
    uint8_t tx[64];
    uint8_t rx[64];
    tx[0] = 0x40 | (addr & 0x3F); // burst write header
    for (uint8_t i = 0; i < len; i++)
        tx[i + 1] = data[i];
    spiTransaction(tx, rx, static_cast<size_t>(len) + 1);
}

/*
 * CRC-16/MODBUS
 */
uint16_t DieselHeaterRF::crc16_2(char *buf, int len) {

  uint16_t crc = 0xFFFF;

  for (int pos = 0; pos < len; pos++) {
    crc ^= (uint8_t)buf[pos];
    for (int i = 8; i != 0; i--) {
      if ((crc & 0x0001) != 0) {
        crc >>= 1;
        crc ^= 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc;
}
