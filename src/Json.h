#pragma once

#include <Arduino.h>
#include <initializer_list>
#include <type_traits>

// Minimal JSON writers used for every JSON text the firmware produces (MQTT state, Home Assistant discovery,
// debug events and the web status). They take care of quoting, escaping and commas, so payloads are built
// the same way everywhere:
//
//     JsonObject().str("mode", "heat").num("temp", 21.5, 1).num("zone", 2).toString()   ->  {"mode":"heat","temp":21.5,"zone":2}
//
// Setters return the object so calls can be chained.

class JsonObject {
public:
    // "key":"value" (the value is escaped)
    JsonObject &str(const String &key, const String &value) {
        writeKey(key);
        _body += "\"" + escape(value) + "\"";
        return *this;
    }

    // "key":123 for any integer type (signed or not)
    template <typename T, typename = typename std::enable_if<std::is_integral<T>::value && !std::is_same<T, bool>::value>::type>
    JsonObject &num(const String &key, T value) {
        writeKey(key);
        _body += std::is_signed<T>::value ? String((long) value) : String((unsigned long) value);
        return *this;
    }

    // "key":21.5 with a fixed number of decimals (always given, so a float is never printed by accident)
    JsonObject &num(const String &key, double value, uint8_t decimals) {
        writeKey(key);
        _body += String(value, (unsigned int) decimals);
        return *this;
    }

    // "key":true / false
    JsonObject &boolean(const String &key, bool value) {
        writeKey(key);
        _body += value ? "true" : "false";
        return *this;
    }

    // "key": <json>, the text is inserted as is (nested object or array already serialised)
    JsonObject &raw(const String &key, const String &json) {
        writeKey(key);
        _body += json;
        return *this;
    }

    // Appends every field of another object
    JsonObject &merge(const JsonObject &other) {
        if (other._body.length() == 0) return *this;
        if (!_first) _body += ',';
        _body += other._body;
        _first = false;
        return *this;
    }

    void reserve(unsigned int size) { _body.reserve(size); }
    String toString() const { return "{" + _body + "}"; }

    static String escape(const String &value) {
        String s = value;
        s.replace("\\", "\\\\");
        s.replace("\"", "\\\"");
        s.replace("\n", "\\n");
        s.replace("\r", "\\r");
        s.replace("\t", "\\t");
        return s;
    }

private:
    void writeKey(const String &key) {
        if (!_first) _body += ',';
        _first = false;
        _body += "\"" + escape(key) + "\":";
    }

    String _body;
    bool _first = true;
};

class JsonArray {
public:
    // "value" (escaped)
    JsonArray &str(const String &value) {
        separator();
        _body += "\"" + JsonObject::escape(value) + "\"";
        return *this;
    }

    // 123 for any integer type (signed or not)
    template <typename T, typename = typename std::enable_if<std::is_integral<T>::value && !std::is_same<T, bool>::value>::type>
    JsonArray &num(T value) {
        separator();
        _body += std::is_signed<T>::value ? String((long) value) : String((unsigned long) value);
        return *this;
    }

    // 21.5 with a fixed number of decimals
    JsonArray &num(double value, uint8_t decimals) {
        separator();
        _body += String(value, (unsigned int) decimals);
        return *this;
    }

    // Inserted as is (nested object or array already serialised)
    JsonArray &raw(const String &json) {
        separator();
        _body += json;
        return *this;
    }

    void reserve(unsigned int size) { _body.reserve(size); }
    String toString() const { return "[" + _body + "]"; }

private:
    void separator() {
        if (!_first) _body += ',';
        _first = false;
    }

    String _body;
    bool _first = true;
};