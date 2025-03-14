#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <time.h>
#include <Preferences.h>  // Biblioteca para guardar datos en la flash

#define SERVICE_UUID        "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

Preferences preferences;  // Objeto para manejar memoria flash

BLEServer* pServer = nullptr;
BLECharacteristic* pTxCharacteristic;
bool deviceConnected = false;

class MyServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) override {
        deviceConnected = true;
        Serial.println("BLE Conectado!");
    }

    void onDisconnect(BLEServer* pServer) override {
        deviceConnected = false;
        Serial.println("BLE Desconectado!");

        delay(500);  // Pequeña espera para evitar problemas
        pServer->getAdvertising()->start();
        Serial.println("Esperando nueva conexión BLE...");
    }
};


class MyCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pCharacteristic) override {
        String rxValue = pCharacteristic->getValue().c_str();  // Conversión corregida
        if (rxValue.length() > 0) {
            Serial.print("Recibido: ");
            Serial.println(rxValue);

            struct tm t;
            sscanf(rxValue.c_str(), "%d-%d-%d %d:%d:%d", &t.tm_year, &t.tm_mon, &t.tm_mday, &t.tm_hour, &t.tm_min, &t.tm_sec);
            t.tm_year -= 1900; // Ajuste para `tm_year`
            t.tm_mon -= 1;     // Ajuste para `tm_mon`
            time_t epochTime = mktime(&t);
            struct timeval now = { .tv_sec = epochTime };
            settimeofday(&now, NULL);

            Serial.println("Fecha y hora actualizada!");
        }
    }
};

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    BLEDevice::init("ESP32_BLE");
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    BLEService* pService = pServer->createService(SERVICE_UUID);
    pTxCharacteristic = pService->createCharacteristic(CHARACTERISTIC_UUID_TX, BLECharacteristic::PROPERTY_NOTIFY);
    BLECharacteristic* pRxCharacteristic = pService->createCharacteristic(CHARACTERISTIC_UUID_RX, BLECharacteristic::PROPERTY_WRITE);
    pRxCharacteristic->setCallbacks(new MyCallbacks());

    pService->start();
    BLEDevice::startAdvertising();
    Serial.println("Esperando conexión BLE...");
}

void loop() {}
