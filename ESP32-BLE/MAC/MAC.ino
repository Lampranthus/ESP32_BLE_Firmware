#include <Arduino.h>
#include <BLEDevice.h>

void setup() {
    Serial.begin(115200);
    Serial.println("Obteniendo dirección MAC del ESP32...");

    BLEDevice::init("ESP32_BLE");
    
    // Obtener y mostrar la dirección MAC en el Monitor Serie
    Serial.print("Dirección MAC del ESP32: ");
    Serial.println(BLEDevice::getAddress().toString().c_str());
}

void loop() {
    // No se necesita código en loop para esta prueba
}