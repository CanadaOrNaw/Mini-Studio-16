#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string>

class NimBLEServer;
class NimBLECharacteristic;

class NimBLEServerCallbacks {
public:
    virtual ~NimBLEServerCallbacks() = default;
    virtual void onConnect(NimBLEServer*) {}
    virtual void onDisconnect(NimBLEServer*) {}
};

class NimBLECharacteristicCallbacks {
public:
    virtual ~NimBLECharacteristicCallbacks() = default;
    virtual void onWrite(NimBLECharacteristic*) {}
};

namespace NIMBLE_PROPERTY {
static const uint32_t READ = 1u << 0;
static const uint32_t WRITE = 1u << 1;
static const uint32_t WRITE_NR = 1u << 2;
static const uint32_t NOTIFY = 1u << 3;
}

class NimBLECharacteristic {
public:
    std::string getValue() const { return {}; }
    void setValue(const uint8_t*, size_t) {}
    void setCallbacks(NimBLECharacteristicCallbacks*) {}
    void notify() {}
};

class NimBLEService {
public:
    NimBLECharacteristic* createCharacteristic(const char*, uint32_t) {
        static NimBLECharacteristic characteristic;
        return &characteristic;
    }
    void start() {}
};

class NimBLEServer {
public:
    void setCallbacks(NimBLEServerCallbacks*) {}
    NimBLEService* createService(const char*) {
        static NimBLEService service;
        return &service;
    }
};

class NimBLEAdvertising {
public:
    void addServiceUUID(const char*) {}
    void setScanResponse(bool) {}
    void start() {}
};

class NimBLEDevice {
public:
    static void init(const char*) {}
    static NimBLEServer* createServer() { static NimBLEServer server; return &server; }
    static NimBLEAdvertising* getAdvertising() {
        static NimBLEAdvertising advertising; return &advertising;
    }
    static void startAdvertising() {}
};
