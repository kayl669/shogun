#include "Shogun.h"

#define VERSION "1.0.0"
#define RS485_BAUD 19200
#define RS485_EN_PIN 21
const uint8_t SHOGUN_ID = 1;
uint8_t currentModbusSlave = SHOGUN_ID;

#define REG_DATETIME 21
#define REG_BASE_TEMPERATURE 333
#define REG_BASE_SETPOINT 92
#define REG_BASE_DAMPERS 332
#define REG_BASE_PROGRAM_STATUS 251
#define REG_BASE_HEATING_STATE 93
#define REG_BASE_COOLING_STATE 173
#define REG_ACTIVE_FAULT 4
#define REG_CONFIGURED_ZONES 11
#define REG_BUILDING_STATE 61
#define REG_AUTO_CHANGEOVER 62
#define REG_SENSOR_LOCK 31
#define REG_SUPPLY_FAN_SPEED 290
#define REG_CURRENT_OUTDOOR_TEMP 320
#define REG_ZONE_NAME 620
#define REG_SCHEDULE_ASSIGNMENT 41
#define REG_SCHEDULE_BASE 401
#define REG_SCHEDULE_WORDS 42

static bool isCoolingBuildingMode(uint16_t buildingState) {
    return buildingState == 2;
}

// Electronics / dampers / indoor unit idle consumption (W) when nothing is heating or cooling.
static constexpr uint16_t ELECTRICAL_STANDBY_W = 10;

// Outdoor temperatures (deg C) at which the heat pump has to deliver its full capacity: the building heat loss is
// proportional to the indoor / outdoor temperature difference, so the steady load is 0 at the indoor temperature and 1 here.
static constexpr float DESIGN_OUTDOOR_HEATING_C = -7.0f;
static constexpr float DESIGN_OUTDOOR_COOLING_C = 35.0f;
// A zone this far beyond its target (above in heating, below in cooling) no longer asks for anything.
static constexpr float SATISFIED_MARGIN_K = 0.5f;

// Load curve used for the transient demand (recovery after a setback or a setpoint change), same curve as FujitsuAC:
// 0.5 K dead band, then steps from 20 to 100 percent of the rated input power.
static float demandLoadFromDelta(float deltaT) {
    constexpr float DEAD_BAND = 0.5f;
    if (deltaT <= DEAD_BAND) return 0.0f;
    const float effectiveDelta = deltaT - DEAD_BAND;
    if (effectiveDelta <= 0.5f) return 0.20f;
    if (effectiveDelta <= 1.0f) return 0.30f;
    if (effectiveDelta <= 2.0f) return 0.45f;
    if (effectiveDelta <= 3.0f) return 0.65f;
    if (effectiveDelta <= 4.0f) return 0.82f;
    return 1.00f;
}

static uint16_t getZoneManualSetpointRegister(uint8_t zone, uint16_t buildingState) {
    if (zone < 1 || zone > 8) return 0;
    if (isCoolingBuildingMode(buildingState)) return 172 + (uint16_t)(zone - 1) * 10;
    return 92 + (uint16_t)(zone - 1) * 10;
}

// Comfort / Eco setpoints used by the time schedules (Temp Holding Registers, tenths of a degree).
// Heating: 90/91 + 10*(zone-1). Cooling: 170/171 + 10*(zone-1). Eco = Comfort + 1.
static uint16_t getZoneComfortRegister(uint8_t zone, uint16_t buildingState) {
    if (zone < 1 || zone > 8) return 0;
    if (isCoolingBuildingMode(buildingState)) return 170 + (uint16_t)(zone - 1) * 10;
    return 90 + (uint16_t)(zone - 1) * 10;
}

static uint16_t getZoneModeRegister(uint8_t zone, uint16_t buildingState) {
    if (zone < 1 || zone > 8) return 0;
    if (isCoolingBuildingMode(buildingState)) return 174 + (uint16_t)(zone - 1) * 10;
    return 94 + (uint16_t)(zone - 1) * 10;
}

static uint16_t getZonePowerRegister(uint8_t zone, uint16_t buildingState) {
    if (zone < 1 || zone > 8) return 0;
    if (isCoolingBuildingMode(buildingState)) return 177 + (uint16_t)(zone - 1) * 10;
    return 97 + (uint16_t)(zone - 1) * 10;
}

RTC_NOINIT_ATTR bool isFallbackAp;
RTC_NOINIT_ATTR int fallbackApReason;

void rs485PreTransmission() { digitalWrite(RS485_EN_PIN, HIGH); }
void rs485PostTransmission() { digitalWrite(RS485_EN_PIN, LOW); }

namespace ShogunAC {
    ShogunAC *ShogunAC::_mqttInstance = nullptr;

    enum FallbackApReason: int {
        None = 0,
        UnableToConnectWiFi = 1,
        UnableToConnectMqtt = 2,
        ResetReasonPanic = 3,
    };

    ShogunAC::ShogunAC(
        int rxPin,
        int txPin,
        int resetButtonPin
    ) : _config(VERSION, rxPin, txPin, resetButtonPin),
        server(80),
        espClient(),
        _mqttClient(espClient) {
        _mqttInstance = this;
    }

    void ShogunAC::setup() {
        _config.load();
        _config.initIO();
        loadEnergy();
        littleFsReady = LittleFS.begin(true);
        if (!littleFsReady) {
            Serial.println("[WEB] LittleFS mount failed");
        }

        this->setupWebServer();
        this->handleResetButton();

        if (this->createAP()) {
            this->setupOTA();

            return;
        }

        this->connectToWifi();
        this->server.begin();
        Serial.println("[WEB] HTTP server started on port 80");
        this->setupOTA();

        _mqttClient.setServer(_config.getMqttIp().c_str(), (uint16_t) _config.getMqttPort().toInt());
        _mqttClient.setBufferSize(8192);
        _mqttClient.setCallback(ShogunAC::mqttCallback);

        pinMode(RS485_EN_PIN, OUTPUT);
        digitalWrite(RS485_EN_PIN, LOW);
        Serial1.begin(RS485_BAUD, SERIAL_8E2, _config.getRxPin(), _config.getTxPin());
        Serial1.setTimeout(200);
        modbus.begin(currentModbusSlave, Serial1);
        modbus.preTransmission(rs485PreTransmission);
        modbus.postTransmission(rs485PostTransmission);
        discoveryDone = false;
    }

    void ShogunAC::setupOTA() {
        String password = _config.getOtaPw();

        if (password.isEmpty()) {
            password = "shogun";
        }

        ArduinoOTA.setHostname(_config.getDeviceName().c_str());
        ArduinoOTA.setPassword(password.c_str());
        ArduinoOTA.begin();
    }

    void ShogunAC::loop() {
        this->handleResetButton();

        if (this->isAPState()) {
            ArduinoOTA.handle();

            server.handleClient();

            if (isFallbackAp && millis() - fallbackApCreatedAt > 300000) {
                isFallbackAp = false;
                fallbackApReason = FallbackApReason::None;

                ESP.restart();
            }

            return;
        }

        if (WiFi.status() != WL_CONNECTED) {
            ESP.restart();
        }

        ArduinoOTA.handle();

        server.handleClient();

        connectToMqtt();
        _mqttClient.loop();
        if (millis() - lastModbusRequestTime > 60) {
            lastModbusRequestTime = millis();
            uint8_t result;
            if (modbusState > 0 && modbusState < 17) {
                while (Serial1.available()) Serial1.read();
            }
            switch (modbusState) {
                case 0: if (millis() - lastPublish > 15000 || lastPublish == 0) {
                        currentCycleSuccess = true;
                        modbusState = 1;
                    }
                    break;
                case 1:
                    result = modbusReadInput(REG_ACTIVE_FAULT, 1, "active_fault");
                    if (result == modbus.ku8MBSuccess) activeFault = modbus.getResponseBuffer(0);
                    else
                        currentCycleSuccess = false;
                    result = modbusReadInput(REG_CONFIGURED_ZONES, 1, "configured_zones");
                    if (result == modbus.ku8MBSuccess) configuredZones = min<uint16_t>(modbus.getResponseBuffer(0), MAX_ZONES);
                    else
                        currentCycleSuccess = false;
                    modbusState = 2;
                    break;
                case 2:
                    currentZoneIndex = 0;
                    result = modbusReadHolding(REG_DATETIME, 6, "datetime");
                    if (result == modbus.ku8MBSuccess) {
                        int day = modbus.getResponseBuffer(0);
                        int month = modbus.getResponseBuffer(1);
                        int year = modbus.getResponseBuffer(2);
                        int day_of_week = modbus.getResponseBuffer(3);
                        int hour = modbus.getResponseBuffer(4);
                        int minute = modbus.getResponseBuffer(5);
                        dateTime = String(day) + "/" + String(month) + "/" + String(year) + " " + String(day_of_week) + " " + String(hour) + ":" + String(minute);
                    } else { currentCycleSuccess = false; }
                    result = modbusReadHolding(REG_BUILDING_STATE, 2, "building_state_auto_changeover");
                    if (result == modbus.ku8MBSuccess) {
                        buildingState = modbus.getResponseBuffer(0);
                        autoChangeover = modbus.getResponseBuffer(1);
                    } else currentCycleSuccess = false;
                    modbusState = 3;
                    break;
                case 3: // Per-zone loop: ambient temperature + active setpoint [Input Reg]
                    result = modbusReadInput(REG_BASE_TEMPERATURE + currentZoneIndex * 3, 2, "zone_temperature_setpoint");
                    if (result == modbus.ku8MBSuccess) {
                        zones[currentZoneIndex].ambientTemp = modbus.getResponseBuffer(0) / 10.0;
                        zones[currentZoneIndex].targetTemp = modbus.getResponseBuffer(1) / 10.0;
                    } else {
                        currentCycleSuccess = false;
                    }
                    modbusState = 4;
                    break;
                case 4: // Per-zone loop: manual setpoint [Holding Reg] + zone name [Input Reg]
                    result = modbusReadHolding(getZoneManualSetpointRegister((uint8_t)(currentZoneIndex + 1), buildingState), 1, "zone_manual_setpoint");
                    if (result == modbus.ku8MBSuccess) {
                        zones[currentZoneIndex].setpointTemp = modbus.getResponseBuffer(0) / 10.0;
                    } else {
                        currentCycleSuccess = false;
                    }
                    result = modbusReadHolding(getZoneComfortRegister((uint8_t)(currentZoneIndex + 1), buildingState), 2, "zone_comfort_eco");
                    if (result == modbus.ku8MBSuccess) {
                        zones[currentZoneIndex].comfortTemp = modbus.getResponseBuffer(0) / 10.0;
                        zones[currentZoneIndex].ecoTemp = modbus.getResponseBuffer(1) / 10.0;
                    } else {
                        currentCycleSuccess = false;
                    }
                    // Min / Max manual setpoint of the zone sit at comfort+5 / comfort+6 (heating and cooling banks).
                    result = modbusReadHolding(getZoneComfortRegister((uint8_t)(currentZoneIndex + 1), buildingState) + 5, 2, "zone_manual_limits");
                    if (result == modbus.ku8MBSuccess) {
                        zones[currentZoneIndex].minSetpoint = modbus.getResponseBuffer(0) / 10.0;
                        zones[currentZoneIndex].maxSetpoint = modbus.getResponseBuffer(1) / 10.0;
                    } else {
                        currentCycleSuccess = false;
                    }
                    result = modbusReadHolding(REG_SENSOR_LOCK + currentZoneIndex, 1, "zone_sensor_lock");
                    if (result == modbus.ku8MBSuccess) {
                        zones[currentZoneIndex].sensorLock = modbus.getResponseBuffer(0);
                    } else {
                        currentCycleSuccess = false;
                    }
                    result = modbusReadHolding(getZoneModeRegister((uint8_t)(currentZoneIndex + 1), buildingState), 1, "zone_mode");
                    if (result == modbus.ku8MBSuccess) {
                        zones[currentZoneIndex].controlMode = modbus.getResponseBuffer(0);
                    } else {
                        currentCycleSuccess = false;
                    }
                    result = modbusReadHolding(REG_ZONE_NAME + currentZoneIndex * 20, 20, "zone_name");
                    if (result == modbus.ku8MBSuccess) {
                        // Zone names are stored as two ASCII bytes per register,
                        // with the bytes in reverse order (e.g. 0x6843 -> "Ch").
                        String name = "";
                        for (uint16_t i = 0; i < 6; ++i) {
                            uint16_t v = modbus.getResponseBuffer(i);
                            char first = (char)(v & 0xFF);
                            char second = (char)((v >> 8) & 0xFF);
                            if (first >= 32 && first <= 126) name += first;
                            if (second >= 32 && second <= 126) name += second;
                        }
                        name.trim();
                        zones[currentZoneIndex].name = name.isEmpty() ? ("Zone " + String(currentZoneIndex)) : name;

                        bool allZoneNamesReady = true;
                        for (uint8_t z = 0; z < configuredZones && z < MAX_ZONES; ++z) {
                            if (zones[z].name.isEmpty()) {
                                allZoneNamesReady = false;
                                break;
                            }
                        }
                        if (allZoneNamesReady && _mqttClient.connected() && !discoveryDone) {
                            setupMqttDiscovery();
                        }
                    } else {
                        currentCycleSuccess = false;
                    }

                    modbusState = 5;
                    break;
                case 5: // Per-zone loop: damper openings [Input Reg]
                    result = modbusReadInput(REG_BASE_DAMPERS + currentZoneIndex, 1, "zone_damper");
                    if (result == modbus.ku8MBSuccess) {
                        zones[currentZoneIndex].damperOpening = modbus.getResponseBuffer(0);
                    } else {
                        currentCycleSuccess = false;
                    }
                    modbusState = 6;
                    break;
                case 6: // Per-zone loop: heating/cooling state + program status [Input Reg]
                {
                    uint16_t stateReg = isCoolingBuildingMode(buildingState) ? (REG_BASE_COOLING_STATE + currentZoneIndex * 10) : (REG_BASE_HEATING_STATE + currentZoneIndex * 10);
                    result = modbusReadInput(stateReg, 1, "zone_heating_state");
                    if (result == modbus.ku8MBSuccess) {
                        zones[currentZoneIndex].heatingState = modbus.getResponseBuffer(0);
                    } else {
                        currentCycleSuccess = false;
                    }
                    result = modbusReadInput(REG_BASE_PROGRAM_STATUS + currentZoneIndex, 1, "zone_program");
                    if (result == modbus.ku8MBSuccess) {
                        zones[currentZoneIndex].programStatus = modbus.getResponseBuffer(0);
                    } else {
                        currentCycleSuccess = false;
                    }
                    currentZoneIndex++;
                    if (currentZoneIndex < configuredZones && currentZoneIndex < MAX_ZONES) {
                        modbusState = 3;
                        // Move to the next zone
                    } else {
                        modbusState = 7;
                        // Proceed to heat pump telemetry
                    }
                    break;
                }
                case 7:
                    // Indoor unit airflow telemetry [Input Reg]
                    result = modbusReadInput(REG_SUPPLY_FAN_SPEED, 5, "indoor_airflow");
                    if (result == modbus.ku8MBSuccess) {
                        averageAmbientTemp = modbus.getResponseBuffer(3) / 10.0;
                        averageSetpointTemp = modbus.getResponseBuffer(4) / 10.0;
                    } else {
                        currentCycleSuccess = false;
                    }
                    modbusState = 11;
                    break;
                case 11:
                    //  outdoor temperature [Input Reg]
                    result = modbusReadInput(REG_CURRENT_OUTDOOR_TEMP, 1, "current_outdoor_temp");
                    if (result == modbus.ku8MBSuccess) {
                        currentOutdoorTemp = (int16_t) modbus.getResponseBuffer(0) / 10.0;
                    } else {
                        currentCycleSuccess = false;
                    }
                    modbusState = 12;
                    break;
                case 12: // Read the hourly schedule assigned to each zone.
                    result = modbusReadHolding(REG_SCHEDULE_ASSIGNMENT, configuredZones, "schedule_assignments");
                    if (result == modbus.ku8MBSuccess) {
                        for (uint8_t i = 0; i < configuredZones && i < MAX_ZONES; ++i) {
                            scheduleAssignments[i] = modbus.getResponseBuffer(i);
                            zones[i].scheduleProgram = scheduleAssignments[i];
                        }
                    } else {
                        currentCycleSuccess = false;
                    }
                    scheduleReadIndex = 0;
                    modbusState = 13;
                    break;
                case 13: // Read one complete heating schedule (42 Time registers).
                {
                    uint16_t start = REG_SCHEDULE_BASE + scheduleReadIndex * REG_SCHEDULE_WORDS;
                    result = modbusReadHolding(start, REG_SCHEDULE_WORDS, "schedule_times");
                    if (result == modbus.ku8MBSuccess) {
                        for (uint8_t i = 0; i < REG_SCHEDULE_WORDS; ++i) {
                            scheduleTimes[scheduleReadIndex][i] = modbus.getResponseBuffer(i);
                        }
                    } else {
                        currentCycleSuccess = false;
                    }
                    scheduleReadIndex++;
                    if (scheduleReadIndex < 4) {
                        modbusState = 13;
                    } else {
                        schedulesRead = true;
                        modbusState = 14;
                    }
                    break;
                }
                case 14: // Finalize read cycle & MQTT publication
                    lastPublish = millis();
                    modbusError = !currentCycleSuccess;
                    if (currentCycleSuccess) {
                        updateEnergyEstimate(millis());
                        publishMqttData();
                    }
                    modbusState = 0;
                    break;
                default: ;
            }
        }
    }

    // Writes the building state (1 heat, 2 cool, 3 dry, 4 off) and reads it back: the Shogun does not always accept it.
    // With the automatic heating/cooling changeover on, the controller chooses the building state itself.
    // buildingState is always set to the value really kept, so the state published to Home Assistant is the real one.
    // Returns 0 when accepted, 1 on a Modbus error, 2 when the controller kept another value.
    uint8_t ShogunAC::writeBuildingMode(uint16_t value, const char *source, String &error) {
        String label;
        while (Serial1.available()) Serial1.read();
        label = String(source) + "_building_mode";
        uint8_t result = modbusWriteSingle(REG_BUILDING_STATE, value, label.c_str());
        if (result != modbus.ku8MBSuccess) { error = "Modbus building mode write failed"; return 1; }

        delay(200);     // let the controller apply the new state before reading it back
        label = String(source) + "_building_mode_verify";
        result = modbusReadHolding(REG_BUILDING_STATE, 1, label.c_str());
        if (result != modbus.ku8MBSuccess) { error = "Building mode verification failed"; return 1; }

        const uint16_t confirmed = modbus.getResponseBuffer(0);
        buildingState = confirmed;
        if (confirmed == value) return 0;

        error = "Building mode was not accepted by the controller (kept " + getBuildingModeText(confirmed) + ")";
        if (autoChangeover) error += ". Automatic heating/cooling changeover is on: the controller chooses the mode itself, turn it off first";
        return 2;
    }

    // Writes the manual (room) setpoint of a zone and switches the zone to manual mode (unless it is OFF, which is
    // preserved), then reads the register back.
    // Returns 0 when the Shogun kept the value, 1 on a Modbus error, 2 when it kept another value (the zone's
    // setpoint then holds that value and `error` describes it, with the zone limits when they can be read).
    uint8_t ShogunAC::writeManualSetpoint(uint8_t zone, float temperature, const char *source, String &error) {
        if (zone < 1 || zone > MAX_ZONES) { error = "Invalid zone"; return 1; }
        const uint8_t zoneIndex = zone - 1;
        const uint16_t setpointReg = getZoneManualSetpointRegister(zone, buildingState);
        const uint16_t value = (uint16_t) roundf(temperature * 10.0f);
        String label;

        while (Serial1.available()) Serial1.read();
        label = String(source) + "_setpoint";
        uint8_t result = modbusWriteSingle(setpointReg, value, label.c_str());
        if (result != modbus.ku8MBSuccess) { error = "Modbus setpoint update failed"; return 1; }

        // A manual setpoint is only active in manual mode.
        if (zones[zoneIndex].programStatus != 3) {
            label = String(source) + "_manual_mode";
            result = modbusWriteSingle(getZoneModeRegister(zone, buildingState), 1, label.c_str());
            if (result != modbus.ku8MBSuccess) { error = "Modbus manual mode update failed"; return 1; }
            zones[zoneIndex].controlMode = 1;
        }

        delay(200);
        label = String(source) + "_setpoint_verify";
        result = modbusReadHolding(setpointReg, 1, label.c_str());
        if (result != modbus.ku8MBSuccess) { error = "Setpoint verification failed"; return 1; }
        const uint16_t kept = modbus.getResponseBuffer(0);
        zones[zoneIndex].setpointTemp = kept / 10.0;
        if (kept == value) return 0;

        error = "Shogun kept " + String(kept / 10.0, 1) + " instead of " + String(temperature, 1);
        label = String(source) + "_setpoint_limits";
        if (modbusReadHolding(getZoneComfortRegister(zone, buildingState) + 5, 2, label.c_str()) == modbus.ku8MBSuccess) {
            error += " (zone limits " + String(modbus.getResponseBuffer(0) / 10.0, 1) + "-" + String(modbus.getResponseBuffer(1) / 10.0, 1) + ")";
        }
        return 2;
    }

    // Edits the setpoint that currently drives a zone. The active setpoint register of the Shogun (input register
    // 334 + 3 * (zone - 1)) is read-only, so the change goes to the register behind it and the zone mode is left as is:
    //   manual mode   -> manual setpoint
    //   schedule mode -> Comfort or Eco setpoint, depending on the slot being applied (it then applies to every slot
    //                    of that kind, not only to the current one)
    // Returns 0 when the Shogun kept the value, 1 on a Modbus error, 2 when it kept another value,
    // 3 when the zone has no editable active setpoint (off or unused).
    uint8_t ShogunAC::writeActiveSetpoint(uint8_t zone, float temperature, const char *source, String &error) {
        if (zone < 1 || zone > MAX_ZONES) { error = "Invalid zone"; return 1; }
        ZoneStatus &z = zones[zone - 1];
        if (z.programStatus == 3 || z.heatingState == 3 || z.heatingState == 0) { error = "Zone is off or unused"; return 3; }

        if (z.controlMode == 1 || z.heatingState == 4) {
            const uint8_t result = writeManualSetpoint(zone, temperature, source, error);
            if (result == 0) z.targetTemp = z.setpointTemp;
            return result;
        }
        if (z.heatingState != 1 && z.heatingState != 2) { error = "No Comfort/Eco slot is active"; return 3; }

        const bool eco = z.heatingState == 2;
        const uint16_t comfortReg = getZoneComfortRegister(zone, buildingState);
        const uint16_t value = (uint16_t) roundf(temperature * 10.0f);
        String label;

        while (Serial1.available()) Serial1.read();
        label = String(source) + "_active_" + (eco ? "eco" : "comfort");
        uint8_t result = modbusWriteSingle(eco ? comfortReg + 1 : comfortReg, value, label.c_str());
        if (result != modbus.ku8MBSuccess) { error = "Modbus setpoint update failed"; return 1; }

        delay(200);
        label = String(source) + "_active_verify";
        result = modbusReadHolding(comfortReg, 2, label.c_str());
        if (result != modbus.ku8MBSuccess) { error = "Setpoint verification failed"; return 1; }
        z.comfortTemp = modbus.getResponseBuffer(0) / 10.0;
        z.ecoTemp = modbus.getResponseBuffer(1) / 10.0;
        const uint16_t kept = modbus.getResponseBuffer(eco ? 1 : 0);
        if (kept == value) {
            z.targetTemp = kept / 10.0;          // refreshed by the next read cycle anyway
            return 0;
        }

        error = "Shogun kept " + String(kept / 10.0, 1) + " instead of " + String(temperature, 1) + " for " + (eco ? "Eco" : "Comfort")
              + (isCoolingBuildingMode(buildingState) ? ". Cooling mode: eco must be >= comfort." : ". Heating mode: eco must be <= comfort.");
        return 2;
    }

    // Estimated electrical input power (W). The Shogun gives no electrical measurement and no usable outdoor unit
    // telemetry, so the load is modelled from the zones and the outdoor temperature, then applied to the nominal
    // electrical power at full load set in the configuration page (heating or cooling).
    //  - Steady load: a heat pump mostly runs to hold the temperature, i.e. to compensate the building heat loss, which
    //    follows the indoor / outdoor temperature difference. It is 0 at the indoor temperature and 1 at the design
    //    outdoor temperature (DESIGN_OUTDOOR_HEATING_C / DESIGN_OUTDOOR_COOLING_C), and it is counted for the share of
    //    zones that still ask for heat (cold): a single zone on out of four only carries a quarter of the building.
    //  - Transient load: a zone far from its target (see demandLoadFromDelta) asks for more. The largest of the two applies.
    //  - Nothing runs when no zone is on, when every zone is beyond its target, or outside heating / cooling mode.
    uint16_t ShogunAC::estimateElectricalPower() {
        const bool heating = buildingState == 1;
        const bool cooling = buildingState == 2;
        if (!heating && !cooling) return ELECTRICAL_STANDBY_W;

        const float rated = heating ? _config.getPowerHeatingW() : _config.getPowerCoolingW();
        if (rated <= 0.0f) return ELECTRICAL_STANDBY_W;

        const uint8_t zoneCount = configuredZones < MAX_ZONES ? configuredZones : MAX_ZONES;
        float askingTargetSum = 0.0f;
        float maxLoad = 0.0f;
        uint8_t askingZones = 0;
        uint8_t callingZones = 0;
        for (uint8_t i = 0; i < zoneCount; i++) {
            const ZoneStatus &z = zones[i];
            if (z.programStatus == 3) continue;                                  // zone switched off
            if (z.ambientTemp < 0.0f || z.ambientTemp > 60.0f) continue;         // sensor missing / invalid
            if (z.targetTemp <= 0.0f || z.targetTemp > 40.0f) continue;          // no valid target yet
            const float deltaT = heating ? (z.targetTemp - z.ambientTemp) : (z.ambientTemp - z.targetTemp);
            if (deltaT >= -SATISFIED_MARGIN_K) {
                askingZones++;
                askingTargetSum += z.targetTemp;
            }
            const float load = demandLoadFromDelta(deltaT);
            if (load > 0.0f) {
                callingZones++;
                if (load > maxLoad) maxLoad = load;
            }
        }
        if (askingZones == 0) return ELECTRICAL_STANDBY_W;     // every zone is off or beyond its target

        // Steady load from the outdoor temperature (ignored when the sensor reading is not plausible)
        float steadyLoad = 0.0f;
        if (currentOutdoorTemp > -40.0f && currentOutdoorTemp < 55.0f) {
            const float indoor = askingTargetSum / (float) askingZones;
            const float span = heating ? (indoor - DESIGN_OUTDOOR_HEATING_C) : (DESIGN_OUTDOOR_COOLING_C - indoor);
            const float gap = heating ? (indoor - currentOutdoorTemp) : (currentOutdoorTemp - indoor);
            if (span >= 1.0f) steadyLoad = constrain(gap / span, 0.0f, 1.0f) * ((float) askingZones / (float) zoneCount);
        }

        // The heat pump serves the most demanding zone; each additional zone calling adds a little.
        float transientLoad = maxLoad;
        if (callingZones > 1) transientLoad += 0.05f * (float) (callingZones - 1);

        float load = steadyLoad > transientLoad ? steadyLoad : transientLoad;
        if (load > 1.0f) load = 1.0f;

        float power = rated * load;
        if (power < ELECTRICAL_STANDBY_W) power = ELECTRICAL_STANDBY_W;
        return (uint16_t) lroundf(power);
    }

    // Estimated energy: integral of the estimated power over time (kWh), like FujitsuAC.
    // Saved to flash every 5 minutes (a reboot loses at most that much), reset from Home Assistant.
    static constexpr uint32_t ENERGY_MAX_GAP_MS = 300000UL;     // ignore longer gaps (Modbus / Wi-Fi outage)
    static constexpr uint32_t ENERGY_SAVE_PERIOD_MS = 300000UL;

    void ShogunAC::loadEnergy() {
        energyPrefs.begin("shogun_energy", false);
        energyKwh = energyPrefs.getDouble("total-kwh", 0.0);
        energyPrefs.end();
        if (!(energyKwh >= 0.0 && energyKwh < 1e9)) energyKwh = 0.0;   // also rejects NaN / infinity
    }

    void ShogunAC::updateEnergyEstimate(uint32_t now) {
        const uint16_t powerW = estimateElectricalPower();

        if (lastEnergySample != 0) {
            uint32_t elapsed = now - lastEnergySample;
            if (elapsed > ENERGY_MAX_GAP_MS) elapsed = ENERGY_MAX_GAP_MS;
            energyKwh += (double) powerW * (double) elapsed / 3600000000.0;     // W * ms -> kWh
        }
        lastEnergySample = now;

        if (now - lastEnergySave >= ENERGY_SAVE_PERIOD_MS) {
            energyPrefs.begin("shogun_energy", false);
            energyPrefs.putDouble("total-kwh", energyKwh);
            energyPrefs.end();
            lastEnergySave = now;
        }
    }

    void ShogunAC::resetEnergy() {
        energyKwh = 0.0;
        energyPrefs.begin("shogun_energy", false);
        energyPrefs.putDouble("total-kwh", 0.0);
        energyPrefs.end();
        lastEnergySave = millis();
        publishMqttDebug("info", JsonObject().str("event", "estimated_energy_reset"));
        lastPublish = 0;
        publishMqttData();
    }

    String ShogunAC::topicFor(const String &suffix) {
        return "shogun/" + _config.getUniqueId() + "/" + suffix;
    }

    String ShogunAC::stateTopicFor(const String &section) {
        return topicFor("state/" + section);
    }

    // Home Assistant only accepts homeassistant/<component>/<node_id>/<object_id>/config.
    String ShogunAC::discoveryTopicFor(const char *component, const String &objectId) {
        return "homeassistant/" + String(component) + "/" + _config.getUniqueId() + "/" + objectId + "/config";
    }

    void ShogunAC::publishMqttState(const String &section, const String &json) {
        if (!_mqttClient.publish(stateTopicFor(section).c_str(), json.c_str(), true)) {
            Serial.println("[MQTT] State publication failed");
        }
    }

    void ShogunAC::publishMqttData() {
        publishMqttState("system", JsonObject()
            .num("central_fault", activeFault)
            .num("detected_zone_count", configuredZones)
            .num("outdoor_temperature", currentOutdoorTemp, 1)
            .num("building_state", buildingState)
            .str("building_mode", getBuildingModeText(buildingState))
            .str("auto_changeover", autoChangeover ? "on" : "off")
            .toString());

        publishMqttState("ui", JsonObject()
            .num("average_ambient_temperature", averageAmbientTemp, 1)
            .num("average_setpoint_temperature", averageSetpointTemp, 1)
            .num("estimated_power_w", estimateElectricalPower())
            .num("estimated_energy_kwh", energyKwh, 4)
            .toString());

        JsonArray assignments;
        for (uint8_t i = 0; i < configuredZones && i < MAX_ZONES; ++i) {
            assignments.num(scheduleAssignments[i]);
        }
        publishMqttState("schedule_assignments", assignments.toString());

        JsonObject schedules;
        for (uint8_t pg = 0; pg < 4; ++pg) {
            JsonArray times;
            for (uint8_t slot = 0; slot < 42; ++slot) {
                times.num(scheduleTimes[pg][slot]);
            }
            schedules.raw("program_" + String(pg + 1), times.toString());
        }
        publishMqttState("schedules", schedules.toString());

        for (uint8_t i = 0; i < configuredZones && i < MAX_ZONES; i++) {
            const ZoneStatus &z = zones[i];
            String climateMode = "off";
            if (z.programStatus != 3) climateMode = isCoolingBuildingMode(buildingState) ? "cool" : "heat";
            publishMqttState("zone_" + String(i + 1), JsonObject()
                .str("name", z.name)
                .num("temp", z.ambientTemp, 1)
                .num("target", z.targetTemp, 1)
                .num("set", z.setpointTemp, 1)
                .str("mode", climateMode)
                .num("control_mode", z.controlMode)
                .num("damper", z.damperOpening)
                .num("prog", z.programStatus)
                .num("comfort", z.comfortTemp, 1)
                .num("eco", z.ecoTemp, 1)
                .num("min_setpoint", z.minSetpoint, 1)
                .num("max_setpoint", z.maxSetpoint, 1)
                .num("sensor_lock", z.sensorLock)
                .str("power", z.programStatus == 3 ? "OFF" : "ON")
                .num("schedule_program", scheduleAssignments[i])
                .toString());
        }
    }

    void ShogunAC::setupMqttDiscovery() {
        if (configuredZones == 0 || discoveryDone) {
            return;
        }

        const String uniqueId = _config.getUniqueId();
        const String availabilityTopic = topicFor("availability");
        String deviceName = _config.getDeviceName();
        deviceName.toLowerCase();

        const String device = JsonObject()
            .raw("identifiers", JsonArray().str("shogun_" + uniqueId).toString())
            .str("name", _config.getDeviceName())
            .str("manufacturer", "Atlantic")
            .str("model", "Shogun ZC 1.1 160 S4")
            .str("sw_version", VERSION)
            .toString();

        bool namesReady = true;
        for (uint8_t i = 0; i < configuredZones && i < MAX_ZONES; ++i) {
            if (zones[i].name.isEmpty()) {
                namesReady = false;
                break;
            }
        }

        // A discovery message. The object id is the last part of its topic, the slug is used for the unique id and
        // the default entity id. The availability and device fields are added when it is published.
        struct Entity {
            const char *component;
            String objectId;
            JsonObject json;
        };
        auto newEntity = [&](const char *component, const String &objectId, const String &slug, const String &name) {
            Entity e{component, objectId, JsonObject()};
            e.json.str("name", name)
                  .str("unique_id", "shogun_" + uniqueId + "_" + slug)
                  .str("default_entity_id", String(component) + "." + deviceName + "_" + slug);
            return e;
        };
        auto publish = [&](Entity &e) {
            e.json.str("availability_topic", availabilityTopic)
                  .str("payload_available", "online")
                  .str("payload_not_available", "offline")
                  .raw("device", device);
            _mqttClient.publish(discoveryTopicFor(e.component, e.objectId).c_str(), e.json.toString().c_str(), true);
        };
        auto globalEntity = [&](const char *component, const String &id, const String &name) {
            return newEntity(component, id, id, name);
        };
        auto addNumberFields = [](JsonObject &json) {
            json.num("min", 10).num("max", 30).num("step", 0.5, 1).str("mode", "box")
                .str("unit_of_measurement", "°C").str("device_class", "temperature");
        };
        auto addOnOffFields = [](JsonObject &json) {
            json.str("payload_on", "ON").str("payload_off", "OFF").str("state_on", "ON").str("state_off", "OFF");
        };
        // Stays "unknown" (None) until the Shogun value has actually been read (0 = not read yet).
        auto numberTemplate = [](const char *key) {
            return "{% set v = value_json." + String(key) + " | float(0) %}{{ v if v > 0 else None }}";
        };

        // Building mode
        Entity e = globalEntity("select", "building_mode", "Building mode");
        e.json.str("state_topic", stateTopicFor("system"))
              .str("value_template", "{{ value_json.building_mode }}")
              .str("command_topic", topicFor("building/mode"))
              .raw("options", JsonArray().str("heat").str("cool").str("dry").str("off").toString())
              .str("icon", "mdi:home-thermometer");
        publish(e);

        e = globalEntity("switch", "auto_changeover", "Automatic heating/cooling changeover");
        e.json.str("state_topic", stateTopicFor("system"))
              .str("value_template", "{{ value_json.auto_changeover }}")
              .str("command_topic", topicFor("building/auto_changeover"))
              .str("payload_on", "on").str("payload_off", "off").str("state_on", "on").str("state_off", "off")
              .str("icon", "mdi:autorenew");
        publish(e);

        // Estimated electrical power. This is a model-based estimate, not a metered value.
        e = globalEntity("sensor", "estimated_power", "Estimated power");
        e.json.str("state_topic", stateTopicFor("ui"))
              .str("value_template", "{{ value_json.estimated_power_w }}")
              .str("unit_of_measurement", "W")
              .str("device_class", "power")
              .str("state_class", "measurement")
              .str("icon", "mdi:flash");
        publish(e);

        // Estimated energy (kWh, usable in the Home Assistant Energy dashboard) and its reset button.
        e = globalEntity("sensor", "estimated_energy", "Estimated energy");
        e.json.str("state_topic", stateTopicFor("ui"))
              .str("value_template", "{{ value_json.estimated_energy_kwh }}")
              .str("unit_of_measurement", "kWh")
              .str("device_class", "energy")
              .str("state_class", "total_increasing")
              .num("suggested_display_precision", 2)
              .str("icon", "mdi:lightning-bolt-circle");
        publish(e);

        e = globalEntity("button", "reset_energy", "Reset estimated energy");
        e.json.str("command_topic", topicFor("system/reset_energy"))
              .str("payload_press", "PRESS")
              .str("entity_category", "config")
              .str("icon", "mdi:counter");
        publish(e);

        for (uint8_t i = 0; i < configuredZones && i < MAX_ZONES; i++) {
            const String zone = String(i + 1);
            const String zoneName = zones[i].name.isEmpty() ? "Zone " + zone : zones[i].name;
            const String zoneStateTopic = stateTopicFor("zone_" + zone);

            // Entity of this zone, read from its state topic and, when `command` is given, driven by zone/<N>/<command>.
            auto zoneEntity = [&](const char *component, const String &suffix, const String &label,
                                  const String &valueTemplate, const String &command) {
                Entity z = globalEntity(component, "zone_" + zone + "_" + suffix, zoneName + " " + label);
                z.json.str("state_topic", zoneStateTopic).str("value_template", valueTemplate);
                if (!command.isEmpty()) z.json.str("command_topic", topicFor("zone/" + zone + "/" + command));
                return z;
            };

            // Thermostat
            e = newEntity("climate", "zone_" + zone, "zone_" + zone + "_climate", zoneName);
            e.json.str("icon", "mdi:air-conditioner")
                  .str("current_temperature_topic", zoneStateTopic)
                  .str("current_temperature_template", "{{ value_json.temp | replace(',', '.') | float(0) }}")
                  .str("temperature_state_topic", zoneStateTopic)
                  .str("temperature_state_template", "{{ value_json.target | replace(',', '.') | float(0) }}")
                  .str("temperature_command_topic", topicFor("zone/" + zone + "/setpoint"))
                  .str("temperature_command_template", "{{ value }}")
                  .str("mode_state_topic", zoneStateTopic)
                  .str("mode_state_template", "{{ value_json.mode }}")
                  .str("mode_command_topic", topicFor("zone/" + zone + "/mode"))
                  .str("mode_command_template", "{{ value }}")
                  .num("min_temp", 10)
                  .num("max_temp", 30)
                  .num("temp_step", 0.5, 1)
                  .num("precision", 0.5, 1)
                  .str("temperature_unit", "C")
                  .raw("modes", JsonArray().str("off").str("heat").str("cool").toString())
                  .str("suggested_area", zoneName);
            publish(e);

            // Read-only sensors
            e = zoneEntity("sensor", "temperature", "Temperature", "{{ value_json.temp }}", "");
            e.json.str("unit_of_measurement", "°C").str("device_class", "temperature").str("state_class", "measurement");
            publish(e);

            e = zoneEntity("sensor", "damper", "Damper", "{{ value_json.damper }}", "");
            e.json.str("unit_of_measurement", "%").str("icon", "mdi:air-filter");
            publish(e);

            e = zoneEntity("sensor", "program", "Program", "{{ value_json.prog }}", "");
            publish(e);

            // Setpoints. Comfort / Eco are used by the time schedules (heating or cooling bank, following the building mode).
            e = zoneEntity("number", "comfort", "Comfort setpoint", numberTemplate("comfort"), "comfort");
            addNumberFields(e.json);
            e.json.str("entity_category", "config").str("icon", "mdi:sofa");
            publish(e);

            e = zoneEntity("number", "eco", "Eco setpoint", numberTemplate("eco"), "eco");
            addNumberFields(e.json);
            e.json.str("entity_category", "config").str("icon", "mdi:leaf");
            publish(e);

            e = zoneEntity("number", "active_setpoint", "Active setpoint", numberTemplate("target"), "active_setpoint");
            addNumberFields(e.json);
            e.json.str("icon", "mdi:thermostat");
            publish(e);

            e = zoneEntity("number", "manual_setpoint", "Manual setpoint", numberTemplate("set"), "setpoint");
            addNumberFields(e.json);
            e.json.str("icon", "mdi:thermometer");
            publish(e);

            e = zoneEntity("number", "min_setpoint", "Min manual setpoint", numberTemplate("min_setpoint"), "min_setpoint");
            addNumberFields(e.json);
            e.json.str("entity_category", "config").str("icon", "mdi:thermometer-low");
            publish(e);

            e = zoneEntity("number", "max_setpoint", "Max manual setpoint", numberTemplate("max_setpoint"), "max_setpoint");
            addNumberFields(e.json);
            e.json.str("entity_category", "config").str("icon", "mdi:thermometer-high");
            publish(e);

            // Modes and switches
            e = zoneEntity("select", "control_mode", "Control mode",
                           "{{ 'manual' if value_json.control_mode | int(0) == 1 else 'schedule' }}", "control_mode");
            e.json.raw("options", JsonArray().str("schedule").str("manual").toString()).str("icon", "mdi:calendar-clock");
            publish(e);

            e = zoneEntity("select", "schedule_program", "Schedule program", "{{ value_json.schedule_program | int(1) }}", "schedule");
            e.json.raw("options", JsonArray().str("1").str("2").str("3").str("4").toString()).str("icon", "mdi:calendar-week");
            publish(e);

            e = zoneEntity("switch", "power", "Power", "{{ value_json.power }}", "power");
            addOnOffFields(e.json);
            e.json.str("icon", "mdi:power");
            publish(e);

            e = zoneEntity("switch", "sensor_lock", "Sensor lock",
                           "{{ 'ON' if value_json.sensor_lock | int(0) == 1 else 'OFF' }}", "sensor_lock");
            addOnOffFields(e.json);
            e.json.str("entity_category", "config").str("icon", "mdi:lock");
            publish(e);
        }

        zoneNamesDiscoveryPublished = namesReady;
        discoveredZoneCount = configuredZones;
        publishMqttDebug("info", JsonObject()
            .str("event", "homeassistant_discovery_published")
            .num("climate_entities", configuredZones)
            .raw("global_controls", JsonArray().str("building_mode").str("auto_changeover").toString())
            .raw("zone_commands", JsonArray().str("active_setpoint").str("setpoint").str("mode").str("comfort").str("eco")
                .str("min_setpoint").str("max_setpoint").str("control_mode").str("schedule").str("power").str("sensor_lock").toString())
            .boolean("zone_names_ready", namesReady));
        discoveryDone=true;
    }

    void ShogunAC::publishMqttAvailability(const char *status) {
        _mqttClient.publish(topicFor("availability").c_str(), status, true);
    }

    void ShogunAC::mqttCallback(char *topic, byte *payload, unsigned int length) {
        if (_mqttInstance == nullptr) return;
        _mqttInstance->mqttCommandCallback(topic, payload, length);
    }

    void ShogunAC::mqttCommandCallback(char *topic, byte *payload, unsigned int length) {
        String topicString(topic);
        String payloadString;
        payloadString.reserve(length + 1);

        for (unsigned int i = 0; i < length; i++) {
            payloadString += (char) payload[i];
        }
        payloadString.trim();

        Serial.print("[MQTT] Command: ");
        Serial.print(topicString);
        Serial.print(" = ");
        Serial.println(payloadString);

        String debugLevelTopic = topicFor("debug/level/set");
        if (topicString == debugLevelTopic) {
            if (!setMqttDebugLevelFromPayload(payloadString)) {
                publishMqttDebug("error", JsonObject().str("event", "debug_level_invalid").str("payload", payloadString).str("accepted", "off,error,info,trace"));
            } else {
                publishMqttDebugLevel();
                publishMqttDebug("info", JsonObject().str("event", "debug_level_changed").str("level", mqttDebugLevelName(_config.getMqttDebugLevel())));
            }
            return;
        }

        String debugTopic = topicFor("debug/command");
        if (topicString == debugTopic) {
            if (payloadString == "level") {
                publishMqttDebugLevel();
            } else {
                publishMqttDebug("warn", JsonObject().str("event", "debug_command_unknown").str("payload", payloadString));
            }
            return;
        }

        handleMqttCommand(topicString, payloadString);
    }

    bool ShogunAC::handleMqttCommand(const String &topic, const String &payload) {
        String buildingTopic = topicFor("building/mode");
        if (topic == buildingTopic) {
            String mode = payload;
            mode.toLowerCase();
            uint16_t value = 0;
            if (mode == "heat") value = 1;
            else if (mode == "cool") value = 2;
            else if (mode == "dry") value = 3;
            else if (mode == "off") value = 4;
            else {
                publishMqttDebug("warn", JsonObject().str("event", "mqtt_command_rejected").str("command", "building_mode").str("payload", payload).str("accepted", "heat,cool,dry,off"));
                return false;
            }
            String error;
            const uint8_t result = writeBuildingMode(value, "mqtt", error);
            if (result != 0) {
                publishMqttDebug("warn", JsonObject().str("event", "mqtt_building_mode_not_applied").str("requested", mode).str("detail", error));
            }
            if (result == 1) return false;
            // Always republish so Home Assistant shows the mode the Shogun really kept.
            lastPublish = 0;
            publishMqttData();
            return result == 0;
        }

        String autoTopic = topicFor("building/auto_changeover");
        if (topic == autoTopic) {
            String valueText = payload;
            valueText.toUpperCase();
            if (valueText != "ON" && valueText != "OFF") return false;
            uint16_t value = valueText == "ON" ? 1 : 0;
            uint8_t result = modbusWriteSingle(REG_AUTO_CHANGEOVER, value, "mqtt_auto_changeover");
            if (result != modbus.ku8MBSuccess) return false;
            autoChangeover = value;
            lastPublish = 0;
            publishMqttData();
            return true;
        }

        String resetEnergyTopic = topicFor("system/reset_energy");
        if (topic == resetEnergyTopic) {
            String valueText = payload;
            valueText.toUpperCase();
            if (valueText != "PRESS" && valueText != "ON" && valueText != "1") return false;
            resetEnergy();
            return true;
        }

        String prefix = topicFor("zone/");
        if (!topic.startsWith(prefix)) return false;

        String remaining = topic.substring(prefix.length());
        int separator = remaining.indexOf('/');
        if (separator < 0) return false;

        int zone = remaining.substring(0, separator).toInt();
        String command = remaining.substring(separator + 1);
        if (zone < 1 || zone > configuredZones || zone > MAX_ZONES) return false;

        uint8_t zoneIndex = (uint8_t)(zone - 1);
        uint16_t modeReg = getZoneModeRegister((uint8_t) zone, buildingState);
        uint16_t powerReg = getZonePowerRegister((uint8_t) zone, buildingState);

        if (command == "setpoint") {
            float temperature = payload.toFloat();
            if (!isfinite(temperature) || temperature < 10.0f || temperature > 30.0f) {
                publishMqttDebug("warn", JsonObject().str("event", "mqtt_command_rejected").num("zone", zone).str("command", "setpoint").str("reason", "range_10_30"));
                return false;
            }

            String error;
            const uint8_t result = writeManualSetpoint((uint8_t) zone, temperature, "mqtt", error);
            if (result == 1) return false;
            if (result == 2) {
                publishMqttDebug("warn", JsonObject().str("event", "mqtt_setpoint_not_kept").num("zone", zone).str("detail", error));
            }
            // Always republish so Home Assistant shows what the Shogun really kept.
            lastPublish = 0;
            publishMqttData();
            return result == 0;
        }

        if (command == "active_setpoint") {
            float temperature = payload.toFloat();
            if (!isfinite(temperature) || temperature < 10.0f || temperature > 30.0f) {
                publishMqttDebug("warn", JsonObject().str("event", "mqtt_command_rejected").num("zone", zone).str("command", "active_setpoint").str("reason", "range_10_30"));
                return false;
            }
            String error;
            const uint8_t result = writeActiveSetpoint((uint8_t) zone, temperature, "mqtt", error);
            if (result != 0) {
                publishMqttDebug("warn", JsonObject().str("event", "mqtt_active_setpoint_not_applied").num("zone", zone).str("detail", error));
            }
            if (result == 1) return false;
            // Always republish so Home Assistant shows what the Shogun really kept.
            lastPublish = 0;
            publishMqttData();
            return result == 0;
        }

        if (command == "comfort" || command == "eco") {
            float temperature = payload.toFloat();
            if (!isfinite(temperature) || temperature < 10.0f || temperature > 30.0f) {
                publishMqttDebug("warn", JsonObject().str("event", "mqtt_command_rejected").num("zone", zone).str("command", command).str("reason", "range_10_30"));
                return false;
            }
            const bool isEco = command == "eco";
            const uint16_t comfortReg = getZoneComfortRegister((uint8_t) zone, buildingState);
            const uint16_t value = (uint16_t) roundf(temperature * 10.0f);
            while (Serial1.available()) Serial1.read();
            uint8_t result = modbusWriteSingle(isEco ? comfortReg + 1 : comfortReg, value, isEco ? "mqtt_eco_setpoint" : "mqtt_comfort_setpoint");
            if (result != modbus.ku8MBSuccess) return false;
            delay(200);
            result = modbusReadHolding(comfortReg, 2, "mqtt_comfort_eco_verify");
            if (result != modbus.ku8MBSuccess) return false;
            zones[zoneIndex].comfortTemp = modbus.getResponseBuffer(0) / 10.0;
            zones[zoneIndex].ecoTemp = modbus.getResponseBuffer(1) / 10.0;
            const bool kept = modbus.getResponseBuffer(isEco ? 1 : 0) == value;
            if (!kept) {
                // The Shogun enforces eco <= comfort (heating) / eco >= comfort (cooling) and zone limits.
                publishMqttDebug("warn", JsonObject().str("event", "mqtt_setpoint_clamped").num("zone", zone).str("command", command).num("requested", temperature, 1).num("comfort", zones[zoneIndex].comfortTemp, 1).num("eco", zones[zoneIndex].ecoTemp, 1));
            }
            // Always republish so Home Assistant shows what the Shogun really kept.
            lastPublish = 0;
            publishMqttData();
            return kept;
        }

        if (command == "min_setpoint" || command == "max_setpoint") {
            float temperature = payload.toFloat();
            if (!isfinite(temperature) || temperature < 10.0f || temperature > 30.0f) {
                publishMqttDebug("warn", JsonObject().str("event", "mqtt_command_rejected").num("zone", zone).str("command", command).str("reason", "range_10_30"));
                return false;
            }
            const bool isMax = command == "max_setpoint";
            const uint16_t limitsReg = getZoneComfortRegister((uint8_t) zone, buildingState) + 5;
            const uint16_t value = (uint16_t) roundf(temperature * 10.0f);
            while (Serial1.available()) Serial1.read();
            uint8_t result = modbusWriteSingle(isMax ? limitsReg + 1 : limitsReg, value, isMax ? "mqtt_max_setpoint" : "mqtt_min_setpoint");
            if (result != modbus.ku8MBSuccess) return false;
            delay(200);
            result = modbusReadHolding(limitsReg, 2, "mqtt_setpoint_limits_verify");
            if (result != modbus.ku8MBSuccess) return false;
            zones[zoneIndex].minSetpoint = modbus.getResponseBuffer(0) / 10.0;
            zones[zoneIndex].maxSetpoint = modbus.getResponseBuffer(1) / 10.0;
            const bool kept = modbus.getResponseBuffer(isMax ? 1 : 0) == value;
            lastPublish = 0;
            publishMqttData();
            return kept;
        }

        if (command == "control_mode") {
            String m = payload;
            m.trim();
            m.toLowerCase();
            uint16_t value;
            if (m == "schedule" || m == "0") value = 0;
            else if (m == "manual" || m == "1") value = 1;
            else return false;
            while (Serial1.available()) Serial1.read();
            uint8_t result = modbusWriteSingle(modeReg, value, value ? "mqtt_manual_mode" : "mqtt_schedule_mode");
            if (result != modbus.ku8MBSuccess) return false;
            zones[zoneIndex].controlMode = value;
            lastPublish = 0;
            publishMqttData();
            return true;
        }

        if (command == "power") {
            String v = payload;
            v.trim();
            v.toUpperCase();
            if (v != "ON" && v != "OFF") return false;
            const bool turnOn = v == "ON";
            while (Serial1.available()) Serial1.read();
            uint8_t result = modbusWriteSingle(powerReg, turnOn ? 1 : 0, turnOn ? "mqtt_zone_on" : "mqtt_zone_off");
            if (result != modbus.ku8MBSuccess) return false;
            // Optimistic status until the next poll (3 = off). The zone keeps its current control mode.
            zones[zoneIndex].programStatus = turnOn ? (zones[zoneIndex].controlMode == 1 ? 4 : 1) : 3;
            lastPublish = 0;
            publishMqttData();
            return true;
        }

        if (command == "schedule") {
            int program = payload.toInt();
            if (program < 1 || program > 4) return false;
            while (Serial1.available()) Serial1.read();
            uint8_t result = modbusWriteSingle(REG_SCHEDULE_ASSIGNMENT + zoneIndex, (uint16_t) program, "mqtt_schedule_program");
            if (result != modbus.ku8MBSuccess) return false;
            result = modbusReadHolding(REG_SCHEDULE_ASSIGNMENT + zoneIndex, 1, "mqtt_schedule_program_verify");
            if (result != modbus.ku8MBSuccess) return false;
            const uint16_t confirmed = modbus.getResponseBuffer(0);
            scheduleAssignments[zoneIndex] = confirmed;
            zones[zoneIndex].scheduleProgram = confirmed;
            lastPublish = 0;
            publishMqttData();
            return confirmed == (uint16_t) program;
        }

        if (command == "sensor_lock") {
            String v = payload;
            v.trim();
            v.toUpperCase();
            if (v != "ON" && v != "OFF") return false;
            const uint16_t value = v == "ON" ? 1 : 0;
            while (Serial1.available()) Serial1.read();
            uint8_t result = modbusWriteSingle(REG_SENSOR_LOCK + zoneIndex, value, "mqtt_sensor_lock");
            if (result != modbus.ku8MBSuccess) return false;
            zones[zoneIndex].sensorLock = value;
            lastPublish = 0;
            publishMqttData();
            return true;
        }

        if (command == "mode") {
            String mode = payload;
            mode.toLowerCase();

            if (mode == "off") {
                while (Serial1.available()) Serial1.read();
                uint8_t result = modbusWriteSingle(powerReg, 0, "mqtt_zone_off");
                if (result != modbus.ku8MBSuccess) return false;
                zones[zoneIndex].programStatus = 3;
                lastPublish = 0;
                publishMqttData();
                return true;
            }

            if (mode == "heat" || mode == "cool") {
                // The Shogun has separate heating and cooling register banks.
                // The building mode selects which bank is active.
                if ((mode == "cool") != isCoolingBuildingMode(buildingState)) return false;
                while (Serial1.available()) Serial1.read();
                uint8_t result = modbusWriteSingle(modeReg, 1, "mqtt_manual_mode");
                if (result != modbus.ku8MBSuccess) return false;
                result = modbusWriteSingle(powerReg, 1, mode == "cool" ? "mqtt_zone_cool_on" : "mqtt_zone_heat_on");
                if (result != modbus.ku8MBSuccess) return false;
                zones[zoneIndex].programStatus = 4;
                lastPublish = 0;
                publishMqttData();
                return true;
            }
        }

        return false;
    }

    const char *ShogunAC::modbusResultText(uint8_t result) const {
        switch (result) {
            case 0x00: return "success";
            case 0x01: return "illegal_function";
            case 0x02: return "illegal_data_address";
            case 0x03: return "illegal_data_value";
            case 0x04: return "slave_device_failure";
            case 0xE0: return "invalid_slave_id";
            case 0xE1: return "invalid_function";
            case 0xE2: return "response_timeout";
            case 0xE3: return "invalid_crc";
            default: return "unknown";
        }
    }

    uint8_t ShogunAC::mqttDebugLevelFor(const char *level) const {
        if (strcmp(level, "error") == 0) return 1;
        if (strcmp(level, "warn") == 0) return 1;
        if (strcmp(level, "info") == 0) return 2;
        if (strcmp(level, "trace") == 0) return 3;
        return 2;
    }

    const char *ShogunAC::mqttDebugLevelName(uint8_t level) const {
        switch (level) {
            case 0: return "off";
            case 1: return "error";
            case 2: return "info";
            case 3: return "trace";
            default: return "error";
        }
    }

    bool ShogunAC::setMqttDebugLevelFromPayload(const String &payload) {
        String value = payload;
        value.trim();
        value.toLowerCase();
        uint8_t level;
        if (value == "off" || value == "0") level = 0;
        else if (value == "error" || value == "errors" || value == "1") level = 1;
        else if (value == "info" || value == "2") level = 2;
        else if (value == "trace" || value == "3") level = 3;
        else return false;
        _config.setMqttDebugLevel(level);
        return true;
    }

    void ShogunAC::publishMqttDebugLevel() {
        if (!_mqttClient.connected()) return;
        _mqttClient.publish(topicFor("debug/level").c_str(), mqttDebugLevelName(_config.getMqttDebugLevel()), true);
    }

    void ShogunAC::publishMqttDebug(const char *level, const JsonObject &event) {
        if (!_mqttClient.connected()) return;
        if (mqttDebugLevelFor(level) > _config.getMqttDebugLevel()) return;
        JsonObject payload;
        payload.num("ts", millis()).str("level", level).merge(event);
        _mqttClient.publish(topicFor("debug").c_str(), payload.toString().c_str(), false);
    }

    // Common fields of the Modbus trace events
    static JsonObject modbusEvent(const char *event, uint8_t function, const char *type, const char *label, uint16_t address) {
        JsonObject json;
        json.str("event", event).num("function", function).str("type", type).str("label", label).num("address", address);
        return json;
    }

    // Completes and publishes the response event of a Modbus request (the register values are listed for reads).
    void ShogunAC::traceModbusResponse(JsonObject &event, uint8_t result, uint32_t elapsedMs, uint16_t valueCount) {
        event.num("slave", currentModbusSlave)
             .str("result", "0x" + String(result, HEX))
             .str("result_text", modbusResultText(result))
             .num("elapsed_ms", elapsedMs);
        if (valueCount > 0 && result == modbus.ku8MBSuccess) {
            JsonArray values;
            for (uint16_t i = 0; i < valueCount; ++i) {
                values.num(modbus.getResponseBuffer(i));
            }
            event.raw("values", values.toString());
        }
        publishMqttDebug(result == modbus.ku8MBSuccess ? "trace" : "error", event);
    }

    uint8_t ShogunAC::modbusReadInput(uint16_t address, uint16_t quantity, const char *label) {
        publishMqttDebug("trace", modbusEvent("modbus_request", 4, "input", label, address).num("quantity", quantity));
        uint32_t start = millis();
        uint8_t result = modbus.readInputRegisters(address - 1, quantity);
        uint32_t elapsed = millis() - start;
        JsonObject response = modbusEvent("modbus_response", 4, "input", label, address);
        response.num("quantity", quantity);
        traceModbusResponse(response, result, elapsed, quantity);
        return result;
    }

    uint8_t ShogunAC::modbusReadHolding(uint16_t address, uint16_t quantity, const char *label) {
        publishMqttDebug("trace", modbusEvent("modbus_request", 3, "holding", label, address).num("quantity", quantity));
        uint32_t start = millis();
        uint8_t result = modbus.readHoldingRegisters(address - 1, quantity);
        uint32_t elapsed = millis() - start;
        JsonObject response = modbusEvent("modbus_response", 3, "holding", label, address);
        response.num("quantity", quantity);
        traceModbusResponse(response, result, elapsed, quantity);
        return result;
    }

    uint8_t ShogunAC::modbusWriteSingle(uint16_t address, uint16_t value, const char *label) {
        publishMqttDebug("trace", modbusEvent("modbus_write_request", 6, "holding", label, address).num("value", value));
        uint32_t start = millis();
        uint8_t result = modbus.writeSingleRegister(address - 1, value);
        uint32_t elapsed = millis() - start;
        JsonObject response = modbusEvent("modbus_write_response", 6, "holding", label, address);
        response.num("value", value);
        traceModbusResponse(response, result, elapsed, 0);
        return result;
    }

    String ShogunAC::getConfigValue(String qs, String key) {
        int start = qs.indexOf(key + "=");
        if (start == -1) return "";

        start += key.length() + 1;
        int end = qs.indexOf("&", start);
        if (end == -1) end = qs.length();

        return this->urlDecode(qs.substring(start, end));
    }

    String ShogunAC::urlDecode(const String &s) {
        String out;
        out.reserve(s.length());

        for (int i = 0; i < s.length(); i++) {
            char c = s[i];

            if (c == '+') {
                out += ' ';
            } else if (c == '%' && i + 2 < s.length()) {
                char h1 = s[i + 1];
                char h2 = s[i + 2];

                int v1 = isdigit(h1) ? h1 - '0' : toupper(h1) - 'A' + 10;
                int v2 = isdigit(h2) ? h2 - '0' : toupper(h2) - 'A' + 10;

                out += char((v1 << 4) | v2);
                i += 2;
            } else {
                out += c;
            }
        }

        return out;
    }

    void ShogunAC::parseConfig(String content) {
        _config.setValue("wifi-ssid", getConfigValue(content, "wifi-ssid"));
        _config.setValue("wifi-pw", getConfigValue(content, "wifi-pw"));
        _config.setValue("mqtt-ip", getConfigValue(content, "mqtt-ip"));
        _config.setValue("mqtt-port", getConfigValue(content, "mqtt-port"));
        _config.setValue("mqtt-user", getConfigValue(content, "mqtt-user"));
        _config.setValue("mqtt-pw", getConfigValue(content, "mqtt-pw"));
        _config.setValue("device-name", getConfigValue(content, "device-name"));
        _config.setValue("ota-pw", getConfigValue(content, "ota-pw"));
        _config.setValue("protocol", getConfigValue(content, "protocol"));
        _config.setValue("pwr-heat", getConfigValue(content, "pwr-heat"));
        _config.setValue("pwr-cool", getConfigValue(content, "pwr-cool"));

        isFallbackAp = false;
        fallbackApReason = FallbackApReason::None;
    }

    void ShogunAC::handleResetButton() {
        if (_config.getResetButtonPin() > 0 && LOW == digitalRead(_config.getResetButtonPin())) {
            _config.clear();

            isFallbackAp = false;
            fallbackApReason = FallbackApReason::None;

            delay(1000);
            ESP.restart();
        }
    }

    bool ShogunAC::isAPState() {
        return isFallbackAp || _config.isEmpty();
    }

    bool ShogunAC::createAP() {
        if (ESP_RST_POWERON == esp_reset_reason()) {
            isFallbackAp = false;
            fallbackApReason = FallbackApReason::None;
        } else if (ESP_RST_PANIC == esp_reset_reason()) {
            isFallbackAp = true;
            fallbackApReason = FallbackApReason::ResetReasonPanic;
        }

        if (!this->isAPState()) {
            return false;
        }

        IPAddress apIP(192, 168, 1, 1);
        IPAddress apGateway(192, 168, 1, 1);
        IPAddress apSubnet(255, 255, 255, 0);

        WiFi.softAPConfig(apIP, apGateway, apSubnet);

        const String accessPointName = "shogun-" + _config.getUniqueId();

        if (!WiFi.softAP(accessPointName.c_str())) {
            ESP.restart();
        }

        this->server.begin();

        if (isFallbackAp) {
            fallbackApCreatedAt = millis();
        }

        return true;
    }

    void ShogunAC::connectToWifi() {
        if (WiFi.status() == WL_CONNECTED) {
            return;
        }

        uint32_t start = millis();

        WiFi.disconnect(true, true);
        WiFi.setHostname(_config.getDeviceName().c_str());
        WiFi.mode(WIFI_STA);

        _config.setWifiSleepEnabled(_config.isWifiSleepEnabled());

        int bestNetwork = -1;
        int bestRSSI = -1000;

        int networkCount = WiFi.scanNetworks();

        for (int i = 0; i < networkCount; i++) {
            int rssi = WiFi.RSSI(i);

            if (
                WiFi.SSID(i) == _config.getWifiSsid()
                && rssi > bestRSSI
            ) {
                bestRSSI = rssi;
                bestNetwork = i;
            }
        }

        if (-1 != bestNetwork) {
            uint8_t *bestBssid = WiFi.BSSID(bestNetwork);
            int channel = WiFi.channel(bestNetwork);

            WiFi.begin(_config.getWifiSsid(), _config.getWifiPw(), channel, bestBssid, true);
        } else {
            WiFi.begin(_config.getWifiSsid(), _config.getWifiPw());
        }

        while (WiFi.status() != WL_CONNECTED) {
            this->handleResetButton();

            if (millis() - start > 60000) {
                isFallbackAp = true;
                fallbackApReason = FallbackApReason::UnableToConnectWiFi;

                ESP.restart();
            }
        }

        Serial.println("\n[Mode Station] Connected to local network.");
        if (MDNS.begin(_config.getDeviceName().c_str())) {
            Serial.printf("[mDNS] http://%s.local\n", _config.getDeviceName().c_str());
            MDNS.addService("http", "tcp", 80);
        }
    }

    void ShogunAC::connectToMqtt() {
        if (_mqttClient.connected()) {
            return;
        }

        uint32_t start = millis();

        while (!_mqttClient.connected()) {
            this->handleResetButton();

            if (WiFi.status() != WL_CONNECTED) {
                ESP.restart();
            }

            String availabilityTopic = topicFor("availability");
            bool connected = false;

            if (_config.getMqttUser() == "") {
                connected = _mqttClient.connect(
                    _config.getDeviceName().c_str(),
                    availabilityTopic.c_str(), 0, true, "offline"
                );
            } else {
                connected = _mqttClient.connect(
                    _config.getDeviceName().c_str(),
                    _config.getMqttUser().c_str(),
                    _config.getMqttPw().c_str(),
                    availabilityTopic.c_str(), 0, true, "offline"
                );
            }

            if (connected) {
                Serial.println("[MQTT] Connected");

                publishMqttAvailability("online");
                publishMqttDebugLevel();

                // Commands: every zone command (active_setpoint, setpoint, mode, comfort, eco, min_setpoint, max_setpoint,
                // control_mode, schedule, power, sensor_lock), building mode, auto changeover, energy reset and debug.
                for (const char *suffix : {"zone/+/+", "building/mode", "building/auto_changeover", "system/reset_energy",
                                           "debug/command", "debug/level/set"}) {
                    _mqttClient.subscribe(topicFor(suffix).c_str());
                }

                publishMqttDebug("info", JsonObject()
                    .str("event", "startup")
                    .num("baud", RS485_BAUD)
                    .str("format", "8E2")
                    .num("slave", currentModbusSlave)
                    .num("tx_gpio", _config.getTxPin())
                    .num("rx_gpio", _config.getRxPin())
                    .num("en_gpio", RS485_EN_PIN)
                    .boolean("en_active_high", true)
                    .str("debug_level", mqttDebugLevelName(_config.getMqttDebugLevel())));
                return;
            }

            Serial.print("[MQTT] Connection failed, state=");
            Serial.println(_mqttClient.state());

            if (millis() - start > 60000) {
                isFallbackAp = true;
                fallbackApReason = FallbackApReason::UnableToConnectMqtt;

                ESP.restart();
            }

            delay(1000);
        }
    }

    String ShogunAC::getBuildingModeText(uint16_t mode) const {
        switch (mode) {
            case 1: return String("heat");
            case 2: return String("cool");
            case 3: return String("dry");
            case 4: return String("off");
            default: return String("unknown");
        }
    }

    String ShogunAC::getProgramText(uint16_t status) {
        switch (status) {
            case 0: return String("Off");
            case 1: return String("Comfort");
            case 2: return String("Eco");
            case 3: return String("Frost Protection");
            default: return String("Override (") + String(status) + String(")");
        }
    }

    String ShogunAC::getModeText(uint16_t mode) {
        switch (mode) {
            case 0: return String("Off");
            case 1: return String("Heating");
            case 2: return String("Cooling");
            case 3: return String("Fan Only");
            default: return String("Unknown");
        }
    }

    String ShogunAC::getFanText(uint16_t fan) {
        switch (fan) {
            case 1: return String("Low");
            case 2: return String("Medium");
            case 3: return String("High");
            default: return String("Auto");
        }
    }

    String ShogunAC::loadResource(const char *path) {
        if (!littleFsReady || !LittleFS.exists(path))return "";
        File f = LittleFS.open(path, "r");
        if (!f)return "";
        String s = f.readString();
        f.close();
        return s;
    }

    void ShogunAC::sendResource(const char *path, const char *type) {
        String s = loadResource(path);
        if (s.isEmpty()) {
            server.send(404, "text/plain", "Resource not found");
            return;
        }
        server.send(200, type, s);
    }

    // Escapes a value inserted in an HTML attribute (the configuration form fields).
    String ShogunAC::htmlEscape(const String &v) {
        String s = v;
        s.replace("&", "&amp;");
        s.replace("\"", "&quot;");
        s.replace("<", "&lt;");
        s.replace(">", "&gt;");
        return s;
    }

    bool ShogunAC::checkWebAuth() {
        if (isAPState()) {
            return true;
        }
        return checkOtaWebAuth();
    }

    bool ShogunAC::checkOtaWebAuth() {
        String password = _config.getOtaPw();
        if (!server.authenticate("admin", password.c_str())) {
            server.requestAuthentication(BASIC_AUTH, "shogun OTA", "Authentication required");
            return false;
        }
        return true;
    }

    void ShogunAC::handleStatus() {
        JsonArray zoneList;
        for (uint8_t i = 0; i < configuredZones && i < MAX_ZONES; ++i) {
            const ZoneStatus &z = zones[i];
            zoneList.raw(JsonObject()
                .num("zone", i + 1)
                .str("name", z.name)
                .num("temp", z.ambientTemp, 1)
                .num("target", z.targetTemp, 1)
                .num("program_status", z.programStatus)
                .num("heating_state", z.heatingState)
                .num("control_mode", z.controlMode)
                .num("schedule_program", scheduleAssignments[i])
                .num("comfort_setpoint", z.comfortTemp, 1)
                .num("eco_setpoint", z.ecoTemp, 1)
                .num("min_setpoint", z.minSetpoint, 1)
                .num("max_setpoint", z.maxSetpoint, 1)
                .toString());
        }

        JsonArray assignments;
        for (uint8_t i = 0; i < configuredZones && i < MAX_ZONES; ++i) {
            assignments.num(scheduleAssignments[i]);
        }

        JsonArray schedules;
        for (uint8_t pg = 0; pg < 4; ++pg) {
            JsonArray times;
            for (uint8_t slot = 0; slot < 42; ++slot) {
                times.num(scheduleTimes[pg][slot]);
            }
            schedules.raw(times.toString());
        }

        JsonObject status;
        status.reserve(16000);
        status.boolean("modbus_error", modbusError)
            .str("mqtt_debug_level", mqttDebugLevelName(_config.getMqttDebugLevel()))
            .str("datetime", dateTime)
            .num("active_fault", activeFault)
            .num("configured_zones", configuredZones)
            .num("building_state", buildingState)
            .str("building_mode", getBuildingModeText(buildingState))
            .str("auto_changeover", autoChangeover ? "on" : "off")
            .num("outdoor_temperature", currentOutdoorTemp, 1)
            .num("estimated_power_w", estimateElectricalPower())
            .num("estimated_energy_kwh", energyKwh, 4)
            .raw("zones", zoneList.toString())
            .raw("schedule_assignments", assignments.toString())
            .raw("schedules", schedules.toString());
        server.sendHeader("Access-Control-Allow-Origin", "*");
        server.send(200, "application/json; charset=utf-8", status.toString());
    }

    void ShogunAC::handleRoot() {
        if (isAPState()) {
            handleConfigGet();
            return;
        }
        String html = loadResource("/index.html");
        if (html.isEmpty()) {
            server.send(500, "text/plain", "Dashboard page unavailable");
            return;
        }
        html.replace("{{DEVICE_NAME}}", _config.getDeviceName());

        server.send(200, "text/html; charset=utf-8", html);
    }

    void ShogunAC::handleAction() {
        if (!checkWebAuth()) {
            return;
        }

        String action = server.arg("cmd");
        if (action == "set_active_temp") {
            if (server.hasArg("zone") && server.hasArg("temp")) {
                int zoneIndex = server.arg("zone").toInt();
                float tempTarget = server.arg("temp").toFloat();
                if (zoneIndex >= 0 && zoneIndex < configuredZones && zoneIndex < MAX_ZONES) {
                    if (!isfinite(tempTarget) || tempTarget < 10.0f || tempTarget > 30.0f) {
                        server.send(400, "text/plain", "Invalid temperature");
                        return;
                    }
                    String error;
                    const uint8_t result = writeActiveSetpoint((uint8_t)(zoneIndex + 1), tempTarget, "web", error);
                    if (result == 1) {
                        server.send(500, "text/plain", error);
                        return;
                    }
                    lastPublish = 0;
                    publishMqttData();
                    if (result == 2 || result == 3) {
                        server.send(409, "text/plain", error);
                        return;
                    }
                    server.send(200, "text/plain", "OK");
                    return;
                }
                server.send(400, "text/plain", "Invalid zone");
                return;
            }
            server.send(400, "text/plain", "Missing zone or temperature");
            return;
        }
        if (action == "set_building_mode") {
            String mode = server.arg("mode");
            mode.toLowerCase();
            uint16_t value = 0;
            if (mode == "heat") value = 1;
            else if (mode == "cool") value = 2;
            else if (mode == "dry") value = 3;
            else if (mode == "off") value = 4;
            else {
                server.send(400, "text/plain", "Invalid building mode");
                return;
            }
            String error;
            const uint8_t result = writeBuildingMode(value, "web", error);
            if (result == 1) {
                server.send(500, "text/plain", error);
                return;
            }
            lastPublish = 0;
            publishMqttData();
            if (result == 2) {
                server.send(409, "text/plain", error);
                return;
            }
            server.send(200, "text/plain", "OK");
            return;
        }
        if (action == "set_auto_changeover") {
            String valueText = server.arg("value");
            valueText.toLowerCase();
            if (valueText != "off" && valueText != "on") {
                server.send(400, "text/plain", "Invalid auto changeover value");
                return;
            }
            uint16_t value = valueText == "on" ? 1 : 0;
            uint8_t result = modbusWriteSingle(REG_AUTO_CHANGEOVER, value, "web_auto_changeover");
            if (result != modbus.ku8MBSuccess) {
                server.send(500, "text/plain", "Modbus write failed");
                return;
            }
            autoChangeover = value;
            lastPublish = 0;
            publishMqttData();
            server.send(200, "text/plain", "OK");
            return;
        }
        if (action == "set_schedule_program") {
            if (server.hasArg("zone") && server.hasArg("program")) {
                // The web API uses human-readable zone numbers: 1..configuredZones.
                int zone = server.arg("zone").toInt();
                int program = server.arg("program").toInt();
                if (zone < 1 || zone > configuredZones || zone > MAX_ZONES || program < 1 || program > 4) {
                    server.send(400, "text/plain", "Invalid zone or schedule program");
                    return;
                }

                const uint8_t zoneIndex = (uint8_t)(zone - 1);
                const uint16_t registerAddress = REG_SCHEDULE_ASSIGNMENT + zoneIndex;

                // The Shogun documentation defines HR 41..48 as writable schedule
                // assignments and expects the program number 1..4.
                uint8_t result = modbusWriteSingle(registerAddress, (uint16_t) program, "web_schedule_assignment");
                if (result != modbus.ku8MBSuccess) {
                    server.send(500, "text/plain", "Modbus schedule assignment failed");
                    return;
                }

                // Read back the register immediately. Do not report success based only
                // on the Modbus write acknowledgement: this confirms that the Shogun
                // actually exposes the requested assignment after the write.
                result = modbusReadHolding(registerAddress, 1, "web_schedule_assignment_verify");
                if (result != modbus.ku8MBSuccess) {
                    server.send(500, "text/plain", "Schedule assignment verification failed");
                    return;
                }

                const uint16_t confirmedProgram = modbus.getResponseBuffer(0);
                if (confirmedProgram != (uint16_t) program) {
                    publishMqttDebug("error", JsonObject().str("event", "schedule_assignment_mismatch").num("zone", zone).num("requested", program).num("confirmed", confirmedProgram));
                    server.send(409, "text/plain", "Shogun rejected the schedule assignment");
                    return;
                }

                scheduleAssignments[zoneIndex] = confirmedProgram;
                zones[zoneIndex].scheduleProgram = confirmedProgram;
                lastPublish = 0;
                publishMqttData();
                server.send(200, "text/plain", "OK");
                return;
            }
            server.send(400, "text/plain", "Missing zone or program");
            return;
        }

        if (action == "set_schedule_time") {
            // program: 1..4, day: 0 (Monday)..6 (Sunday), slot: 0..5 (Comfort1, Eco1, Comfort2, Eco2, Comfort3, Eco3)
            // time: "HH:MM", or "none"/empty to disable the slot (Shogun value -1 = unused).
            if (!server.hasArg("program") || !server.hasArg("day") || !server.hasArg("slot") || !server.hasArg("time")) {
                server.send(400, "text/plain", "Missing program, day, slot or time");
                return;
            }
            int program = server.arg("program").toInt();
            int day = server.arg("day").toInt();
            int slot = server.arg("slot").toInt();
            String timeArg = server.arg("time");
            timeArg.trim();
            if (program < 1 || program > 4 || day < 0 || day > 6 || slot < 0 || slot > 5) {
                server.send(400, "text/plain", "Invalid program, day or slot");
                return;
            }

            uint16_t value = 0xFFFF;
            if (timeArg.length() > 0 && timeArg != "none") {
                int colon = timeArg.indexOf(':');
                if (colon < 1) {
                    server.send(400, "text/plain", "Invalid time format");
                    return;
                }
                int hour = timeArg.substring(0, colon).toInt();
                int minute = timeArg.substring(colon + 1).toInt();
                if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
                    server.send(400, "text/plain", "Invalid time");
                    return;
                }
                // Shogun "Time" coding: high byte = minutes, low byte = hours.
                value = (uint16_t)((minute << 8) | hour);
            }

            const uint8_t offset = (uint8_t)(day * 6 + slot);
            const uint16_t registerAddress = REG_SCHEDULE_BASE + (program - 1) * REG_SCHEDULE_WORDS + offset;

            uint8_t result = modbusWriteSingle(registerAddress, value, "web_schedule_time");
            if (result != modbus.ku8MBSuccess) {
                server.send(500, "text/plain", "Modbus schedule time write failed");
                return;
            }
            result = modbusReadHolding(registerAddress, 1, "web_schedule_time_verify");
            if (result != modbus.ku8MBSuccess) {
                server.send(500, "text/plain", "Schedule time verification failed");
                return;
            }
            const uint16_t confirmed = modbus.getResponseBuffer(0);
            if (confirmed != value) {
                server.send(409, "text/plain", "Shogun rejected the schedule time");
                return;
            }
            scheduleTimes[program - 1][offset] = confirmed;
            lastPublish = 0;
            publishMqttData();
            server.send(200, "text/plain", "OK");
            return;
        }

        if (action == "set_zone_temps") {
            // zone: 1..configuredZones, or "all". comfort / eco: degrees C, either one may be omitted/empty.
            // Writes the Comfort/Eco setpoints of the register set matching the current building mode.
            if (!server.hasArg("zone")) {
                server.send(400, "text/plain", "Missing zone");
                return;
            }
            String zoneArg = server.arg("zone");
            zoneArg.trim();
            zoneArg.toLowerCase();
            String comfortArg = server.arg("comfort");
            comfortArg.trim();
            String ecoArg = server.arg("eco");
            ecoArg.trim();
            const bool hasComfort = comfortArg.length() > 0;
            const bool hasEco = ecoArg.length() > 0;
            if (!hasComfort && !hasEco) {
                server.send(400, "text/plain", "Missing comfort or eco temperature");
                return;
            }

            float comfort = hasComfort ? comfortArg.toFloat() : 0.0f;
            float eco = hasEco ? ecoArg.toFloat() : 0.0f;
            if ((hasComfort && (!isfinite(comfort) || comfort < 10.0f || comfort > 30.0f)) ||
                (hasEco && (!isfinite(eco) || eco < 10.0f || eco > 30.0f))) {
                server.send(400, "text/plain", "Invalid temperature (10-30 C)");
                return;
            }

            int first, last;
            if (zoneArg == "all") {
                first = 0;
                last = (int) configuredZones - 1;
            } else {
                int zone = zoneArg.toInt();
                if (zone < 1 || zone > configuredZones || zone > MAX_ZONES) {
                    server.send(400, "text/plain", "Invalid zone");
                    return;
                }
                first = last = zone - 1;
            }
            if (last >= MAX_ZONES) last = MAX_ZONES - 1;

            while (Serial1.available()) Serial1.read();
            const uint16_t comfortRaw = (uint16_t) roundf(comfort * 10.0f);
            const uint16_t ecoRaw = (uint16_t) roundf(eco * 10.0f);
            for (int zoneIndex = first; zoneIndex <= last; ++zoneIndex) {
                const uint16_t comfortReg = getZoneComfortRegister((uint8_t)(zoneIndex + 1), buildingState);
                const uint16_t ecoReg = comfortReg + 1;
                bool applied = false;
                // Two passes: the Shogun may refuse an intermediate state (e.g. comfort/eco ordering),
                // so a second pass lets both new values land.
                for (int pass = 0; pass < 2 && !applied; ++pass) {
                    if (hasComfort) {
                        uint8_t result = modbusWriteSingle(comfortReg, comfortRaw, "web_comfort_setpoint");
                        if (result != modbus.ku8MBSuccess) {
                            server.send(500, "text/plain", "Modbus comfort setpoint write failed");
                            return;
                        }
                    }
                    if (hasEco) {
                        uint8_t result = modbusWriteSingle(ecoReg, ecoRaw, "web_eco_setpoint");
                        if (result != modbus.ku8MBSuccess) {
                            server.send(500, "text/plain", "Modbus eco setpoint write failed");
                            return;
                        }
                    }
                    // Read both back to confirm what the Shogun actually kept (short pause: let it apply the write).
                    delay(200);
                    uint8_t result = modbusReadHolding(comfortReg, 2, "web_comfort_eco_verify");
                    if (result != modbus.ku8MBSuccess) {
                        server.send(500, "text/plain", "Setpoint verification failed");
                        return;
                    }
                    const uint16_t keptComfort = modbus.getResponseBuffer(0);
                    const uint16_t keptEco = modbus.getResponseBuffer(1);
                    zones[zoneIndex].comfortTemp = keptComfort / 10.0;
                    zones[zoneIndex].ecoTemp = keptEco / 10.0;
                    applied = (!hasComfort || keptComfort == comfortRaw) && (!hasEco || keptEco == ecoRaw);
                }
                if (!applied) {
                    String msg = "Zone " + String(zoneIndex + 1) + ": sent comfort " + (hasComfort ? String(comfort, 1) : String("-"))
                                 + " / eco " + (hasEco ? String(eco, 1) : String("-"))
                                 + ", Shogun kept comfort " + String(zones[zoneIndex].comfortTemp, 1)
                                 + " / eco " + String(zones[zoneIndex].ecoTemp, 1);
                    // Min/Max manual setpoint of the zone sit right after the mode register (comfort+5 / +6).
                    if (modbusReadHolding(comfortReg + 5, 2, "web_setpoint_limits") == modbus.ku8MBSuccess) {
                        msg += " (zone limits " + String(modbus.getResponseBuffer(0) / 10.0, 1) + "-" + String( modbus.getResponseBuffer(1) / 10.0, 1) + ")";
                    }
                    msg += isCoolingBuildingMode(buildingState)
                               ? ". Cooling mode: eco must be >= comfort."
                               : ". Heating mode: eco must be <= comfort.";
                    server.send(409, "text/plain", msg);
                    return;
                }
            }
            lastPublish = 0;
            publishMqttData();
            server.send(200, "text/plain", "OK");
            return;
        }

        if (action == "set_mode") {
            if (!server.hasArg("zone") || !server.hasArg("mode")) {
                server.send(400, "text/plain", "Missing zone or mode");
                return;
            }
            int zoneIndex = server.arg("zone").toInt();
            int mode = server.arg("mode").toInt();
            if (zoneIndex < 0 || zoneIndex >= configuredZones || zoneIndex >= MAX_ZONES || (mode != 0 && mode != 1)) {
                server.send(400, "text/plain", "Invalid zone or mode");
                return;
            }
            uint8_t zone = (uint8_t)(zoneIndex + 1);
            uint8_t result = modbusWriteSingle(getZoneModeRegister(zone, buildingState), (uint16_t)mode, mode == 1 ? "web_manual_mode" : "web_schedule_mode");
            if (result != modbus.ku8MBSuccess) {
                server.send(500, "text/plain", "Modbus zone mode update failed");
                return;
            }
            zones[zoneIndex].controlMode = (uint16_t)mode;
            lastPublish = 0;
            publishMqttData();
            server.send(200, "text/plain", "OK");
            return;
        }
        if (action == "zone_on" || action == "zone_off") {
            if (!server.hasArg("zone")) {
                server.send(400, "text/plain", "Missing zone");
                return;
            }
            int zoneIndex = server.arg("zone").toInt();
            if (zoneIndex < 0 || zoneIndex >= configuredZones || zoneIndex >= MAX_ZONES) {
                server.send(400, "text/plain", "Invalid zone");
                return;
            }
            uint8_t zone = (uint8_t)(zoneIndex + 1);
            bool turnOn = action == "zone_on";
            uint16_t powerReg = getZonePowerRegister(zone, buildingState);
            uint8_t result = modbusWriteSingle(powerReg, turnOn ? 1 : 0, turnOn ? "web_zone_on" : "web_zone_off");
            if (result != modbus.ku8MBSuccess) {
                server.send(500, "text/plain", "Modbus zone power update failed");
                return;
            }
            if (turnOn) {
                result = modbusWriteSingle(getZoneModeRegister(zone, buildingState), 1, "web_manual_mode");
                if (result != modbus.ku8MBSuccess) {
                    server.send(500, "text/plain", "Modbus manual mode update failed");
                    return;
                }
                zones[zoneIndex].controlMode = 1;
                zones[zoneIndex].programStatus = 4;
            } else {
                zones[zoneIndex].programStatus = 3;
            }
            lastPublish = 0;
            publishMqttData();
            server.send(200, "text/plain", "OK");
            return;
        }

        server.sendHeader("Location", String("/"), true);
        server.send(302, "text/plain", "");
    }

    void ShogunAC::handleConfigGet() {
        if (!checkWebAuth()) {
            return;
        }

        String s = loadResource("/config.html");
        s.replace("{{WIFI_SSID}}", htmlEscape(_config.getWifiSsid()));
        s.replace("{{WIFI_PW}}", htmlEscape(_config.getWifiPw()));
        s.replace("{{MQTT_IP}}", htmlEscape(_config.getMqttIp()));
        s.replace("{{MQTT_PORT}}", htmlEscape(_config.getMqttPort()));
        s.replace("{{MQTT_USER}}", htmlEscape(_config.getMqttUser()));
        s.replace("{{MQTT_PW}}", htmlEscape(_config.getMqttPw()));
        s.replace("{{DEVICE_NAME}}", htmlEscape(_config.getDeviceName()));
        s.replace("{{OTA_PW}}", htmlEscape(_config.getOtaPw()));
        s.replace("{{PWR_HEAT}}", String(_config.getPowerHeatingW(), 0));
        s.replace("{{PWR_COOL}}", String(_config.getPowerCoolingW(), 0));
        if (s.isEmpty()) {
            server.send(500, "text/plain", "Web resources unavailable");
            return;
        }
        server.send(200, "text/html; charset=utf-8", s);
    }

    void ShogunAC::handleConfigPost() {
        if (!checkWebAuth()) {
            return;
        }

        String content;
        for (uint8_t i = 0; i < server.args(); i++) {
            if (i)content += '&';
            content += server.argName(i);
            content += '=';
            content += server.arg(i);
        }
        parseConfig(content);
        String s = loadResource("/message.html");
        s.replace("{{TITLE}}", "Configuration");
        s.replace("{{COUNT}}", "5");
        s.replace("{{MESSAGE}}", "<div class=\"notice success\">Configuration saved. Restarting…</div>");
        server.send(200, "text/html; charset=utf-8", s);
        delay(2000);
        ESP.restart();
    }

    void ShogunAC::handleRestart() {
        if (!checkWebAuth()) {
            return;
        }

        String s = loadResource("/message.html");
        s.replace("{{TITLE}}", "ESP32 restart");
        s.replace("{{MESSAGE}}", "The board is restarting. Automatic reconnection in:");
        s.replace("{{COUNT}}", "10");
        server.send(200, "text/html; charset=utf-8", s);
        delay(1500);
        ESP.restart();
    }

    void ShogunAC::setupWebServer() {
        server.on("/", HTTP_GET, [this]() { handleRoot(); });
        server.on("/style.css", HTTP_GET, [this]() { sendResource("/style.css", "text/css; charset=utf-8"); });
        server.on("/app.js", HTTP_GET, [this]() { sendResource("/app.js", "application/javascript; charset=utf-8"); });
        server.on("/api/status", HTTP_GET, [this]() { handleStatus(); });
        server.on("/action", HTTP_POST, [this]() { handleAction(); });
        server.on("/action", HTTP_GET, [this]() { handleAction(); });
        server.on("/config", HTTP_GET, [this]() { handleConfigGet(); });
        server.on("/config", HTTP_POST, [this]() { handleConfigPost(); });
        server.on("/restart", HTTP_GET, [this]() { handleRestart(); });
        server.onNotFound([this]() { server.send(404, "text/plain", "Not found"); });
    }
}