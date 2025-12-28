# ---------- Stage 1: build ----------
FROM ubuntu:22.04 AS build

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    libmosquitto-dev \
    pkg-config \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy project
COPY CMakeLists.txt .
COPY include ./include
COPY src ./src

# Configure & build native for the target arch (build this on the Pi, or use buildx for arm64)
RUN cmake -S . -B build -G Ninja \
 && cmake --build build --config Release \
 && cmake --install build

# ---------- Stage 2: runtime ----------
FROM alpine:3.20

# Runtime deps: mosquitto libs and C++ runtime
RUN apk add --no-cache \
    libstdc++ \
    mosquitto-libs

# Create a non-root user (optional, can skip if you need full /sys access)
# RUN adduser -D heater

WORKDIR /app

# Copy only the installed binary from the build stage
COPY --from=build /usr/local/bin/diesel_heater /usr/local/bin/diesel_heater

# Default env (can be overridden at runtime)
ENV MQTT_HOST=localhost
ENV MQTT_PORT=1883

# Persist heater address
RUN mkdir -p /data
VOLUME ["/data"]

ENTRYPOINT ["/usr/local/bin/diesel_heater"]
