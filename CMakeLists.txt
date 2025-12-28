cmake_minimum_required(VERSION 3.16)
project(diesel_heater_rpi CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Cross-compiler: adjust if you use a different triplet. [web:72]
if(NOT CMAKE_CXX_COMPILER)
    set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
endif()

include_directories(${CMAKE_SOURCE_DIR}/include)

add_executable(diesel_heater
    src/main.cpp
    src/DieselHeaterRF.cpp
)

target_link_libraries(diesel_heater
    mosquitto
)

install(TARGETS diesel_heater
        RUNTIME DESTINATION /usr/local/bin)