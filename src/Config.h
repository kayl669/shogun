#pragma once

#include <Preferences.h>
#include <WiFi.h>

namespace ShogunAC {
    class Config {
    	public:
    		Config(
                const char *version,
                int rxPin,
                int txPin,
                int resetButtonPin
            );

    		~Config();

            void load();
    		void clear();
    		bool isEmpty();
            void initIO();

    		String getUniqueId() { return _uniqueId; }

    		void setValue(const char* key, String value);

    		const char* getVersion() { return _version; }

            void setWifiSleepEnabled(bool status);
            bool isWifiSleepEnabled();

            uint8_t getMqttDebugLevel() const { return _mqttDebugLevel; }
            void setMqttDebugLevel(uint8_t level);

            int getRxPin() { return _rxPin; }
            int getTxPin() { return _txPin; }
            int getResetButtonPin() { return _resetButtonPin; }

    		String getWifiSsid() { return _wifiSsid; }
    		String getWifiPw() { return _wifiPw; }
    		const String& getMqttIp() { return _mqttIp; }
    		String getMqttPort() { return _mqttPort; }
    		String getMqttUser() { return _mqttUser; }
    		String getMqttPw() { return _mqttPw; }
    		String getDeviceName() { return _deviceName; }
    		String getOtaPw() { return _otaPw; }
            float getPowerHeatingW() const { return _powerHeatingW; }   // nominal electrical power, heating (W)
            float getPowerCoolingW() const { return _powerCoolingW; }   // nominal electrical power, cooling (W)

        private:
            Preferences _preferences;

            String _uniqueId;
            const char *_version;

            int _rxPin;
            int _txPin;

            int _resetButtonPin;

            String _wifiSsid;
            String _wifiPw;
            String _mqttIp;
            String _mqttPort;
            String _mqttUser;
            String _mqttPw;
            String _deviceName;
            String _otaPw;
            float _powerHeatingW = 790.0f;
            float _powerCoolingW = 600.0f;

            bool _wifiSleepEnabled = true;
            uint8_t _mqttDebugLevel = 1; // 0=off, 1=errors/warnings, 2=info, 3=trace

            void generateUniqueId();
    };
}