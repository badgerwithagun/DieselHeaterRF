#pragma once
#include <cstdint>
#include <cstddef>
#include <stdexcept>
#include <linux/spi/spidev.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>

// Simple singleton-style SPI instance.
//
// CS is managed manually via GPIO in the CC1101 primitives so that the
// CHIP_RDYn (MISO) signal can be sampled before the first clock edge.
// SPI_MODE_0 (without SPI_NO_CS) is used so the hardware CE0 line also
// tracks CS — this is harmless because each transfer_buf call is one
// atomic SPI_IOC_MESSAGE covering the full CC1101 transaction.
class PiSPI {
    int fd_;
    uint32_t speed_;
public:
    PiSPI(const char *device = "/dev/spidev0.0",
          uint32_t speed = 4000000)
        : fd_(-1), speed_(speed) {

        fd_ = ::open(device, O_RDWR);
        if (fd_ < 0)
            throw std::runtime_error("open spidev failed");

        uint8_t mode = SPI_MODE_0;
        uint8_t bits = 8;
        if (ioctl(fd_, SPI_IOC_WR_MODE, &mode) < 0)
            throw std::runtime_error("SPI_IOC_WR_MODE failed");
        if (ioctl(fd_, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0)
            throw std::runtime_error("SPI_IOC_WR_BITS_PER_WORD failed");
        if (ioctl(fd_, SPI_IOC_WR_MAX_SPEED_HZ, &speed_) < 0)
            throw std::runtime_error("SPI_IOC_WR_MAX_SPEED_HZ failed");
    }

    ~PiSPI() {
        if (fd_ >= 0) ::close(fd_);
    }

    // Transfer len bytes atomically in a single SPI_IOC_MESSAGE call.
    // CS must be managed externally (assert before, deassert after).
    void transfer_buf(const uint8_t *tx, uint8_t *rx, size_t len) {
        struct spi_ioc_transfer tr;
        std::memset(&tr, 0, sizeof(tr));
        tr.tx_buf        = (unsigned long)tx;
        tr.rx_buf        = (unsigned long)rx;
        tr.len           = len;
        tr.speed_hz      = speed_;
        tr.bits_per_word = 8;
        tr.delay_usecs   = 0;
        if (ioctl(fd_, SPI_IOC_MESSAGE(1), &tr) < 1)
            throw std::runtime_error("SPI transfer failed");
    }
};

// Global SPI instance, used by DieselHeaterRF.cpp and main.cpp.
extern PiSPI g_spi;
