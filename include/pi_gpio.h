#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>
#include <fcntl.h>

// Logical constants, mapped onto sysfs semantics.
constexpr int PI_INPUT  = 0;
constexpr int PI_OUTPUT = 1;
constexpr int PI_LOW    = 0;
constexpr int PI_HIGH   = 1;

// Helper: write a string to a file.
inline bool gpioWriteFile(const char *path, const char *value) {
    int fd = ::open(path, O_WRONLY);
    if (fd < 0) return false;
    ssize_t n = ::write(fd, value, std::strlen(value));
    ::close(fd);
    return n == (ssize_t)std::strlen(value);
}

// Export a GPIO if needed.
inline bool gpioExport(int pin) {
    char path[] = "/sys/class/gpio/export";
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d", pin);
    return gpioWriteFile(path, buf);
}

// Set direction.
inline bool gpioSetDirection(int pin, bool isOutput) {
    char path[64];
    std::snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/direction", pin);
    return gpioWriteFile(path, isOutput ? "out" : "in");
}

// Set value.
inline bool gpioSetValue(int pin, int value) {
    char path[64];
    std::snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", pin);
    return gpioWriteFile(path, value ? "1" : "0");
}

// Get value.
inline int gpioGetValue(int pin) {
    char path[64];
    std::snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", pin);
    int fd = ::open(path, O_RDONLY);
    if (fd < 0) return 0;
    char ch = '0';
    if (::read(fd, &ch, 1) != 1) {
        ::close(fd);
        return 0;
    }
    ::close(fd);
    return (ch == '0') ? 0 : 1;
}

// Public API used by DieselHeaterRF.cpp

inline void pinModePi(int pin, int mode) {
    // Best-effort: ignore export errors if already exported.
    gpioExport(pin);
    gpioSetDirection(pin, mode == PI_OUTPUT);
}

inline void digitalWritePi(int pin, int value) {
    gpioSetValue(pin, value != 0);
}

inline int digitalReadPi(int pin) {
    return gpioGetValue(pin);
}
