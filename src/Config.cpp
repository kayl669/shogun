#include "Config.h"

namespace ShogunAC {
    Config::Config(
        const char *version,
        int rxPin,
        int txPin,
        int resetButtonPin
    ):
    	_preferences(),
    	_version(version),
        _rxPin(rxPin),
        _txPin(txPin),
        _resetButtonPin(resetButtonPin)
	{
    }

	Config::~Config() {
	    _preferences.end();
	}

    void Config::generateUniqueId() {
        char buf[13];
        snprintf(buf, sizeof(buf), "%012llX", ESP.getEfuseMac());

        String uniqueId = buf;
        uniqueId.toLowerCase();

        _uniqueId = uniqueId;
    }

    void Config::load() {
        this->generateUniqueId();

        _preferences.begin("shogun_ac", false);

        _wifiSsid = _preferences.getString("wifi-ssid", "");
        _wifiPw = _preferences.getString("wifi-pw", "");
        _mqttIp = _preferences.getString("mqtt-ip", "");
        _mqttPort = _preferences.getString("mqtt-port", "");
        _mqttUser = _preferences.getString("mqtt-user", "");
        _mqttPw = _preferences.getString("mqtt-pw", "");
        _deviceName = _preferences.getString("device-name", "");
        _otaPw = _preferences.getString("ota-pw", "");
        // Nominal electrical power used by the power estimate (empty / invalid -> default)
        auto readWatts = [this](const char *key, float def) {
            String v = _preferences.getString(key, "");
            v.trim();
            if (v.isEmpty()) return def;
            float f = v.toFloat();
            if (!(f >= 0.0f)) return def;       // also rejects NaN
            return f > 20000.0f ? 20000.0f : f;
        };
        _powerHeatingW = readWatts("pwr-heat", 790.0f);
        _powerCoolingW = readWatts("pwr-cool", 600.0f);
        _wifiSleepEnabled = _preferences.getBool("wifi-sleep", true);
        _mqttDebugLevel = _preferences.getUChar("mqtt-debug-level", 1);
        if (_mqttDebugLevel > 3) _mqttDebugLevel = 1;
    }

    void Config::clear() {
	    _preferences.clear();
	}

	bool Config::isEmpty() {
		return _wifiSsid == "";
	}

	void Config::setValue(const char* key, String value) {
		_preferences.putString(key, value);
	}

    void Config::initIO() {
        pinMode(_rxPin, INPUT_PULLUP);
        pinMode(_txPin, OUTPUT);
        digitalWrite(_txPin, LOW);

        if (_resetButtonPin > 0) {
            pinMode(_resetButtonPin, INPUT_PULLUP);
        }
    }

    void Config::setWifiSleepEnabled(bool status)
    {
        if (_wifiSleepEnabled != status) {
            _wifiSleepEnabled = status;
            _preferences.putBool("wifi-sleep", status);
        }

        WiFi.setSleep(status);
    }

    bool Config::isWifiSleepEnabled() {
        return _wifiSleepEnabled;
    }

    void Config::setMqttDebugLevel(uint8_t level) {
        if (level > 3) level = 1;
        _mqttDebugLevel = level;
        _preferences.putUChar("mqtt-debug-level", level);
    }
}