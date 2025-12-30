// src/main.cpp
#include <iostream>
#include <fstream>
#include <string>
#include <cstring>
#include <csignal>
#include <thread>
#include <atomic>
#include <chrono>

#include <mosquitto.h>          // libmosquitto [web:72]

#include "DieselHeaterRF.h"
#include "pi_arduino_compat.h"
#include "pi_gpio.h"
#include "pi_spi.h"

// Global SPI instance used by DieselHeaterRF
PiSPI g_spi("/dev/spidev0.0", 4000000);

static std::atomic<bool> g_running{true};
static std::atomic<uint8_t> g_last_state_code{HEATER_STATE_OFF};

// MQTT / HA configuration
static const char *MQTT_USER      = nullptr;          // or "user"
static const char *MQTT_PASS      = nullptr;          // or "pass"
static const char *CLIENT_ID      = "diesel_heater";
static const char *ADDR_FILE      = "/data/addr.txt"; // path on Pi

// Base topics
static const std::string BASE      = "home/diesel_heater/";

// High-level control topics
static const std::string T_POWER_C = BASE + "power/set";
static const std::string T_POWER_S = BASE + "power/state";
static const std::string T_MODE_C  = BASE + "mode/set";
static const std::string T_MODE_S  = BASE + "mode/state";
static const std::string T_PAIR_C  = BASE + "pair/set";
static const std::string T_PAIR_S  = BASE + "pair/state";

// Low-level command topics
static const std::string T_CMD_WAKEUP = BASE + "cmd/wakeup";
static const std::string T_CMD_MODE   = BASE + "cmd/mode";
static const std::string T_CMD_POWER  = BASE + "cmd/power";
static const std::string T_CMD_UP     = BASE + "cmd/up";
static const std::string T_CMD_DOWN   = BASE + "cmd/down";

// State and sensor topics
static const std::string T_STATE_RAW  = BASE + "state/raw";
static const std::string T_TEMP       = BASE + "ambient_temp";
static const std::string T_VOLT       = BASE + "voltage";
static const std::string T_CASE       = BASE + "case_temp";
static const std::string T_PFREQ      = BASE + "pump_freq";
static const std::string T_HSTATE     = BASE + "state_code";
static const std::string T_HSTATE_TXT = BASE + "state/text";
static const std::string T_RSSI       = BASE + "rssi";

// Availability
static const std::string T_AVAIL      = BASE + "status";

// Discovery topics
static const std::string DISC_POWER   = "homeassistant/switch/diesel_heater/power/config";
static const std::string DISC_PAIR    = "homeassistant/switch/diesel_heater/pair/config";
static const std::string DISC_MODE    = "homeassistant/select/diesel_heater/mode/config";
static const std::string DISC_TEMP    = "homeassistant/sensor/diesel_heater/ambient_temp/config";
static const std::string DISC_VOLT    = "homeassistant/sensor/diesel_heater/voltage/config";
static const std::string DISC_CASE    = "homeassistant/sensor/diesel_heater/case_temp/config";
static const std::string DISC_PFREQ   = "homeassistant/sensor/diesel_heater/pump_freq/config";
static const std::string DISC_HSTATE  = "homeassistant/sensor/diesel_heater/state_code/config";
static const std::string DISC_HTEXT   = "homeassistant/sensor/diesel_heater/state_text/config";
static const std::string DISC_RSSI    = "homeassistant/sensor/diesel_heater/rssi/config";

std::string get_env_or(const char *name, const char *fallback) {
    const char *v = std::getenv(name);
    if (v && *v) return std::string(v);
    return std::string(fallback);
}

int get_env_int_or(const char *name, int fallback) {
    const char *v = std::getenv(name);
    if (!v || !*v) return fallback;
    try {
        return std::stoi(v);
    } catch (...) {
        return fallback;
    }
}

// Helper: load/save heater address
uint32_t load_address() {
    std::ifstream f(ADDR_FILE);
    if (!f) return 0;
    uint32_t addr = 0;
    f >> std::hex >> addr;
    return addr;
}

void save_address(uint32_t addr) {
    std::ofstream f(ADDR_FILE, std::ios::trunc);
    if (!f) return;
    f << std::hex << addr;
}

// MQTT publish helper
void mqtt_publish(struct mosquitto *mosq, const std::string &topic,
                  const std::string &payload, bool retain = false) {
    mosquitto_publish(mosq, nullptr, topic.c_str(),
                      (int)payload.size(), payload.data(), 0,
                      retain ? 1 : 0);
}

// Decode numeric state to text
std::string heater_state_to_str(uint8_t code) {
    switch (code) {
        case HEATER_STATE_OFF:           return "off";
        case HEATER_STATE_STARTUP:       return "startup";
        case HEATER_STATE_WARMING:       return "warming";
        case HEATER_STATE_WARMING_WAIT:  return "warming_wait";
        case HEATER_STATE_PRE_RUN:       return "pre_run";
        case HEATER_STATE_RUNNING:       return "running";
        case HEATER_STATE_SHUTDOWN:      return "shutdown";
        case HEATER_STATE_SHUTTING_DOWN: return "shutting_down";
        case HEATER_STATE_COOLING:       return "cooling";
        default:                         return "unknown";
    }
}

bool heater_is_on(uint8_t state_code) {
    switch (state_code) {
        case HEATER_STATE_OFF:
            return false;
        case HEATER_STATE_SHUTDOWN:
        case HEATER_STATE_SHUTTING_DOWN:
        case HEATER_STATE_COOLING:
            return false;
        case HEATER_STATE_STARTUP:
        case HEATER_STATE_WARMING:
        case HEATER_STATE_WARMING_WAIT:
        case HEATER_STATE_PRE_RUN:
        case HEATER_STATE_RUNNING:
            return true;
        default:
            return false;
    }
}

void publish_discovery(struct mosquitto *mosq) {
    // Common device JSON fragment
    const std::string device_json =
        R"("device":{"identifiers":["diesel_heater"],)"
        R"("name":"Diesel Heater",)"
        R"("manufacturer":"Generic",)"
        R"("model":"CC1101 RF Bridge"})";

    // Power switch
    mqtt_publish(mosq, DISC_POWER,
        R"({"name":"Diesel Heater Power","unique_id":"diesel_heater_power",)"
        R"("command_topic":")" + T_POWER_C +
        R"(","state_topic":")" + T_POWER_S +
        R"(","availability_topic":")" + T_AVAIL +
        R"(","icon":"mdi:fire",)" +
        device_json + "}", true);

    // Pairing switch
    mqtt_publish(mosq, DISC_PAIR,
        R"({"name":"Diesel Heater Pair","unique_id":"diesel_heater_pair",)"
        R"("command_topic":")" + T_PAIR_C +
        R"(","state_topic":")" + T_PAIR_S +
        R"(","availability_topic":")" + T_AVAIL +
        R"(","icon":"mdi:link",)" +
        device_json + "}", true);

    // Mode select
    mqtt_publish(mosq, DISC_MODE,
        R"({"name":"Diesel Heater Mode","unique_id":"diesel_heater_mode",)"
        R"("command_topic":")" + T_MODE_C +
        R"(","state_topic":")" + T_MODE_S +
        R"(","availability_topic":")" + T_AVAIL +
        R"(","options":["auto","manual"],)"
        R"("icon":"mdi:thermostat",)" +
        device_json + "}", true);

    // Ambient temperature
    mqtt_publish(mosq, DISC_TEMP,
        R"({"name":"Diesel Heater Ambient Temperature","unique_id":"diesel_heater_ambient_temp",)"
        R"("state_topic":")" + T_TEMP +
        R"(","availability_topic":")" + T_AVAIL +
        R"(","unit_of_measurement":"°C","device_class":"temperature","state_class":"measurement",)"
        R"("icon":"mdi:thermometer",)" +
        device_json + "}", true);

    // Voltage
    mqtt_publish(mosq, DISC_VOLT,
        R"({"name":"Diesel Heater Voltage","unique_id":"diesel_heater_voltage",)"
        R"("state_topic":")" + T_VOLT +
        R"(","availability_topic":")" + T_AVAIL +
        R"(","unit_of_measurement":"V","device_class":"voltage","state_class":"measurement",)"
        R"("icon":"mdi:current-dc",)" +
        device_json + "}", true);

    // Case temp
    mqtt_publish(mosq, DISC_CASE,
        R"({"name":"Diesel Heater Case Temperature","unique_id":"diesel_heater_case_temp",)"
        R"("state_topic":")" + T_CASE +
        R"(","availability_topic":")" + T_AVAIL +
        R"(","unit_of_measurement":"°C","device_class":"temperature","state_class":"measurement",)"
        R"("icon":"mdi:thermometer-lines",)" +
        device_json + "}", true);

    // Pump frequency
    mqtt_publish(mosq, DISC_PFREQ,
        R"({"name":"Diesel Heater Pump Frequency","unique_id":"diesel_heater_pump_freq",)"
        R"("state_topic":")" + T_PFREQ +
        R"(","availability_topic":")" + T_AVAIL +
        R"(","unit_of_measurement":"Hz","state_class":"measurement",)"
        R"("icon":"mdi:pulse",)" +
        device_json + "}", true);

    // Heater state (numeric)
    mqtt_publish(mosq, DISC_HSTATE,
        R"({"name":"Diesel Heater State Code","unique_id":"diesel_heater_state_code",)"
        R"("state_topic":")" + T_HSTATE +
        R"(","availability_topic":")" + T_AVAIL +
        R"(","icon":"mdi:numeric",)" +
        device_json + "}", true);

    // Heater state (text)
    mqtt_publish(mosq, DISC_HTEXT,
        R"({"name":"Diesel Heater State","unique_id":"diesel_heater_state_text",)"
        R"("state_topic":")" + T_HSTATE_TXT +
        R"(","availability_topic":")" + T_AVAIL +
        R"(","icon":"mdi:information",)" +
        device_json + "}", true);

    // RSSI
    mqtt_publish(mosq, DISC_RSSI,
        R"({"name":"RSSI","unique_id":"diesel_heater_rssi",)"
        R"("state_topic":")" + T_RSSI +
        R"(","availability_topic":")" + T_AVAIL +
        R"(","unit_of_measurement":"dBm","device_class":"signal_strength","state_class":"measurement",)"
        R"("icon":"mdi:signal",)" +
        device_json + "}", true);
}

// Handle MQTT commands → RF commands and pairing
void handle_command(DieselHeaterRF &heater,
                    const std::string &topic,
                    const std::string &payload,
                    struct mosquitto *mosq,
                    uint32_t &heater_addr) {

    std::cout << "Received command: " << topic << ", with payload: " << payload << "\n" << std::flush;
    if (topic == T_POWER_C) {
        if (heater_addr == 0) return;

        std::string desired = payload;
        for (auto &c : desired) c = std::toupper(static_cast<unsigned char>(c));
        bool want_on = (desired == "ON");
        bool want_off = (desired == "OFF");
        if (!want_on && !want_off) return;

        uint8_t current_state = g_last_state_code.load(std::memory_order_relaxed);
        bool is_on = heater_is_on(current_state);

        if ((want_on && !is_on) || (want_off && is_on)) {
            heater.sendCommand(HEATER_CMD_POWER);
        }
        // Optimistically publish; will be kept in sync by state loop
        mqtt_publish(mosq, T_POWER_S, want_on ? "ON" : "OFF");
    } else if (topic == T_CMD_POWER) {
        // raw power toggle, for debugging/advanced use
        if (heater_addr == 0) return;
        heater.sendCommand(HEATER_CMD_POWER);
    } else if (topic == T_CMD_WAKEUP) {
        if (heater_addr == 0) return;
        heater.sendCommand(HEATER_CMD_WAKEUP);
    } else if (topic == T_CMD_MODE) {
        if (heater_addr == 0) return;
        heater.sendCommand(HEATER_CMD_MODE);
    } else if (topic == T_CMD_UP) {
        if (heater_addr == 0) return;
        heater.sendCommand(HEATER_CMD_UP);
    } else if (topic == T_CMD_DOWN) {
        if (heater_addr == 0) return;
        heater.sendCommand(HEATER_CMD_DOWN);
    } else if (topic == T_MODE_C) {
        if (heater_addr == 0) return;
        heater.sendCommand(HEATER_CMD_MODE);
        if (payload == "auto") {
            mqtt_publish(mosq, T_MODE_S, "auto");
        } else if (payload == "manual") {
            mqtt_publish(mosq, T_MODE_S, "manual");
        }
    } else if (topic == T_PAIR_C) {
        if (payload == "ON") {
            mqtt_publish(mosq, T_PAIR_S, "ON");
            std::cout << "Starting pairing...\n" << std::flush;
            uint32_t addr = heater.findAddress(60000);
            if (addr != 0) {
                std::cout << "Paired heater address: 0x" << std::hex << addr << std::dec << "\n" << std::flush;
                heater_addr = addr;
                heater.setAddress(addr);
                save_address(addr);
            } else {
                std::cout << "Pairing timed out, no address found.\n" << std::flush;
            }
            mqtt_publish(mosq, T_PAIR_S, "OFF");
        }
    }
}

// MQTT callback
void on_message(struct mosquitto *mosq, void *userdata,
                const struct mosquitto_message *msg) {
    if (!msg || !msg->topic) return;
    auto *heater = static_cast<DieselHeaterRF*>(userdata);

    static uint32_t heater_addr_cache = 0;
    if (heater_addr_cache == 0) {
        heater_addr_cache = load_address();
        if (heater_addr_cache != 0) {
            heater->setAddress(heater_addr_cache);
        }
    }

    std::string topic((const char*)msg->topic);
    std::string payload;
    if (msg->payload && msg->payloadlen > 0) {
        payload.assign((const char*)msg->payload, msg->payloadlen);
    }

    handle_command(*heater, topic, payload, mosq, heater_addr_cache);
}

// Poll heater state and publish to MQTT
void state_loop(DieselHeaterRF &heater, struct mosquitto *mosq) {
    heater_state_t st{};
    while (g_running) {
        if (heater.getState(&st, 1000)) {
            // remember last state code
            g_last_state_code.store(st.state, std::memory_order_relaxed);
            bool is_on = heater_is_on(st.state);

            mqtt_publish(mosq, T_TEMP,  std::to_string(st.ambientTemp));
            mqtt_publish(mosq, T_VOLT,  std::to_string(st.voltage));
            mqtt_publish(mosq, T_CASE,  std::to_string(st.caseTemp));
            mqtt_publish(mosq, T_PFREQ, std::to_string(st.pumpFreq));
            mqtt_publish(mosq, T_HSTATE,     std::to_string(st.state));
            mqtt_publish(mosq, T_HSTATE_TXT, heater_state_to_str(st.state));
            mqtt_publish(mosq, T_RSSI,       std::to_string(st.rssi));
            mqtt_publish(mosq, T_POWER_S, is_on ? "ON" : "OFF");

            std::string raw = "{"
                              "\"state\":" + std::to_string(st.state) + "," +
                              "\"power\":" + std::to_string(st.power) + "," +
                              "\"voltage\":" + std::to_string(st.voltage) + "," +
                              "\"ambientTemp\":" + std::to_string(st.ambientTemp) + "," +
                              "\"caseTemp\":" + std::to_string(st.caseTemp) + "," +
                              "\"setpoint\":" + std::to_string(st.setpoint) + "," +
                              "\"autoMode\":" + std::to_string(st.autoMode) + "," +
                              "\"pumpFreq\":" + std::to_string(st.pumpFreq) + "," +
                              "\"rssi\":" + std::to_string(st.rssi) +
                              "}";
            mqtt_publish(mosq, T_STATE_RAW, raw);
        }
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
    std::cout << "Exited state listener\n" << std::flush;
}

void handle_signal(int) {
    std::cout << "Exit request received\n" << std::flush;
    g_running = false;
}

int main() {
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    // Resolve MQTT connection parameters from environment
    std::string mqtt_host = get_env_or("MQTT_HOST", "localhost");
    int mqtt_port         = get_env_int_or("MQTT_PORT", 1883);
    std::cout << "Using MQTT host " << mqtt_host << ":" << std::to_string(mqtt_port) << "\n" << std::flush;

    DieselHeaterRF heater;
    heater.begin();
    std::cout << "Radio initialised\n" << std::flush;

    uint32_t addr = load_address();
    if (addr != 0) {
        std::cout << "Using heater address: 0x" << std::hex << addr << std::dec << "\n" << std::flush;
        heater.setAddress(addr);
    } else {
        std::cout << "No saved address; use MQTT pairing switch.\n" << std::flush;
    }

    mosquitto_lib_init();
    struct mosquitto *mosq = mosquitto_new(CLIENT_ID, true, &heater);
    if (!mosq) {
        std::cerr << "mosquitto_new failed\n" << std::flush;
        return 1;
    }

    if (MQTT_USER && MQTT_PASS) {
        mosquitto_username_pw_set(mosq, MQTT_USER, MQTT_PASS);
    }

    mosquitto_message_callback_set(mosq, on_message);

    if (mosquitto_connect(mosq, mqtt_host.c_str(), mqtt_port, 60) != MOSQ_ERR_SUCCESS) {
        std::cerr << "Failed to connect to MQTT\n" << std::flush;
        mosquitto_destroy(mosq);
        mosquitto_lib_cleanup();
        return 1;
    }
    std::cout << "MQTT connected\n" << std::flush;

    // Subscribe to all command topics
    mosquitto_subscribe(mosq, nullptr, T_POWER_C.c_str(), 0);
    mosquitto_subscribe(mosq, nullptr, T_MODE_C.c_str(), 0);
    mosquitto_subscribe(mosq, nullptr, T_PAIR_C.c_str(), 0);
    mosquitto_subscribe(mosq, nullptr, T_CMD_WAKEUP.c_str(), 0);
    mosquitto_subscribe(mosq, nullptr, T_CMD_MODE.c_str(), 0);
    mosquitto_subscribe(mosq, nullptr, T_CMD_POWER.c_str(), 0);
    mosquitto_subscribe(mosq, nullptr, T_CMD_UP.c_str(), 0);
    mosquitto_subscribe(mosq, nullptr, T_CMD_DOWN.c_str(), 0);
    std::cout << "Subscribed to topics\n" << std::flush;

    // Announce discovery
    publish_discovery(mosq);
    std::cout << "Published HA discovery topics\n" << std::flush;

    // Start state loop
    std::thread t_state(state_loop, std::ref(heater), mosq);
    std::cout << "Started state listener\n" << std::flush;

    // Available
    mqtt_publish(mosq, T_AVAIL, "online", true);

    // MQTT loop
    while (g_running) {
        int rc = mosquitto_loop(mosq, 1000, 1);
        if (rc != MOSQ_ERR_SUCCESS) {
            std::cerr << "MQTT loop error (" << rc << "), reconnecting...\n" << std::flush;
            std::this_thread::sleep_for(std::chrono::seconds(2));
            mosquitto_reconnect(mosq);
        }
    }
    std::cout << "Exited MQTT listener\n" << std::flush;

    t_state.join();
    mqtt_publish(mosq, T_AVAIL, "offline", true);
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
    std::cout << "Shutdown complete\n" << std::flush;
    return 0;
}
