#pragma once
#include <cstdint>
#include <chrono>
#include <thread>

inline uint32_t millis() {
    using namespace std::chrono;
    static auto start = std::chrono::steady_clock::now();
    return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - start)
        .count();
}

inline void delay(uint32_t ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}
