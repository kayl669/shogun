#pragma once
#include <Arduino.h>

//WiFi
#include <WiFi.h>

// Access point
#include <WebServer.h>
#include <LittleFS.h>
#include <NetworkClient.h>
#include <WiFiAP.h>
#include <ModbusMaster.h>

//OTA
#include <ESPmDNS.h>
#include <NetworkUdp.h>
#include <ArduinoOTA.h>

//MQTT
#include <PubSubClient.h>

#include <Config.h>
#include <Json.h>

namespace ShogunAC {
    struct ZoneStatus {
        float ambientTemp = 0.0;
        float targetTemp = 0.0;
        float setpointTemp = 0.0;
        float comfortTemp = 0.0;
        float ecoTemp = 0.0;
        float minSetpoint = 0.0;
        float maxSetpoint = 0.0;
        uint16_t sensorLock = 0;
        uint16_t damperOpening = 0;
        uint16_t programStatus = 0;
        uint16_t heatingState = 0;
        uint16_t controlMode = 0;
        uint16_t scheduleProgram = 0;
        String name = "";
    };

    class ShogunAC {
    public:
        ShogunAC(
            int rxPin,
            int txPin,
            int resetButtonPin
        );

        void setup();
        void loop();

        void publishMqttState(const String &section, const String &json);

    private:
        Config _config;
        ModbusMaster modbus;

        WebServer server;

        WiFiClient espClient;
        PubSubClient _mqttClient;

        uint32_t fallbackApCreatedAt = 0;
        bool webOtaAuthorized = false;
        bool littleFsReady = false;
        bool discoveryDone = false;

        static constexpr uint8_t MAX_ZONES = 8;
        ZoneStatus zones[MAX_ZONES];
        String dateTime = "";
        bool modbusError = true;

        uint16_t activeFault = 0;
        uint16_t configuredZones = 0;

        // Estimated energy (integral of the estimated power), persisted in flash
        double energyKwh = 0.0;
        uint32_t lastEnergySample = 0;
        uint32_t lastEnergySave = 0;
        Preferences energyPrefs;
        uint16_t buildingState = 0;
        uint16_t autoChangeover = 0;
        float averageAmbientTemp = 0.0;
        float averageSetpointTemp = 0.0;
        float currentOutdoorTemp = 0.0;

        unsigned long lastModbusRequestTime = 0;
        int modbusState = 0;
        int currentZoneIndex = 0;
        bool currentCycleSuccess = true;
        unsigned long lastPublish = 0;
        bool zoneNamesDiscoveryPublished = false;
        uint8_t discoveredZoneCount = 0;

        // Four heating schedules, 7 days x 3 Comfort/Eco pairs = 42 Time registers each.
        uint16_t scheduleAssignments[MAX_ZONES] = {};
        uint16_t scheduleTimes[4][42] = {};
        uint8_t scheduleReadIndex = 0;
        bool schedulesRead = false;

        String getConfigValue(String qs, String key);
        String urlDecode(const String &s);
        void parseConfig(String content);

        void handleResetButton();

        bool isAPState();
        bool createAP();
        void setupOTA();
        void setupWebServer();

        void connectToWifi();
        void connectToMqtt();
        void setupMqttDiscovery();
        void publishMqttAvailability(const char *status);
        void publishMqttDebug(const char *level, const JsonObject &event);
        void traceModbusResponse(JsonObject &event, uint8_t result, uint32_t elapsedMs, uint16_t valueCount);

        // Topics: shogun/<unique_id>/<suffix>, shogun/<unique_id>/state/<section>
        // and homeassistant/<component>/<unique_id>/<object_id>/config
        String topicFor(const String &suffix);
        String stateTopicFor(const String &section);
        String discoveryTopicFor(const char *component, const String &objectId);
        void publishMqttDebugLevel();
        uint8_t mqttDebugLevelFor(const char *level) const;
        const char *mqttDebugLevelName(uint8_t level) const;
        bool setMqttDebugLevelFromPayload(const String &payload);
        const char *modbusResultText(uint8_t result) const;
        uint8_t modbusReadInput(uint16_t address, uint16_t quantity, const char *label);
        uint8_t modbusReadHolding(uint16_t address, uint16_t quantity, const char *label);
        uint8_t modbusWriteSingle(uint16_t address, uint16_t value, const char *label);
        void mqttCommandCallback(char *topic, byte *payload, unsigned int length);
        bool handleMqttCommand(const String &topic, const String &payload);
        bool checkWebAuth();
        bool checkOtaWebAuth();

        static void mqttCallback(char *topic, byte *payload, unsigned int length);
        static ShogunAC *_mqttInstance;

        String getBuildingModeText(uint16_t mode) const;
        String getProgramText(uint16_t status);
        String getModeText(uint16_t mode);
        String getFanText(uint16_t fan);
        String loadResource(const char *path);
        void sendResource(const char *path, const char *contentType);
        String htmlEscape(const String &value);
        void handleRoot();
        void handleStatus();
        void handleAction();
        void handleConfigGet();
        void handleConfigPost();
        void handleRestart();
        void publishMqttData();
        uint16_t estimateElectricalPower();
        uint8_t writeBuildingMode(uint16_t value, const char *source, String &error);
        uint8_t writeManualSetpoint(uint8_t zone, float temperature, const char *source, String &error);
        uint8_t writeActiveSetpoint(uint8_t zone, float temperature, const char *source, String &error);
        void loadEnergy();
        void updateEnergyEstimate(uint32_t now);
        void resetEnergy();
    };
}