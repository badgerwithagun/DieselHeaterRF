# ---------- Stage 1: build ----------
FROM alpine:3.20 AS build

RUN apk add --no-cache \
    build-base \
    cmake \
    ninja \
    mosquitto-dev \
    pkgconfig \
    linux-headers

WORKDIR /app

COPY CMakeLists.txt .
COPY include ./include
COPY src ./src

# Configure & build with Ninja
RUN cmake -S . -B build -G Ninja \
 && cmake --build build --config Release \
 && cmake --install build

# ---------- Stage 2: runtime ----------
FROM alpine:3.20

RUN apk add --no-cache \
    libstdc++ \
    mosquitto-libs

WORKDIR /app

COPY --from=build /usr/local/bin/diesel_heater /usr/local/bin/diesel_heater

ENV MQTT_HOST=localhost
ENV MQTT_PORT=1883

RUN mkdir -p /data
VOLUME ["/data"]

ENTRYPOINT ["/usr/local/bin/diesel_heater"]
