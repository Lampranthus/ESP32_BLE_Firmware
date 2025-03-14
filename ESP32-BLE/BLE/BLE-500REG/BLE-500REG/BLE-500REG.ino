#include <Arduino.h>
#include <SPIFFS.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <time.h>  // Librería para manejar el reloj interno
#include <Preferences.h>  // Biblioteca para guardar datos en la flash
#include <BLE2902.h>

Preferences preferences;  // Objeto para manejar memoria flash

String secuenciaIngresada = ""; // Almacena la secuencia de teclas ingresadas
const String claveSecreta = "1234"; // Define la clave secreta para activar BLE

// BLE UUIDs
#define SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"


BLEServer* pServer = nullptr;

BLECharacteristic* pRxCharacteristic;
BLECharacteristic* pTxCharacteristic;
bool deviceConnected = false;
bool horaActualizada = false;
bool reconect = false;

int indice = 1;
String mapaTeclas[] = {"F1", "F2", "F3", "F4", "1", "2", "3", "A", "4", "5", "6", "B", "7", "8", "9", "C", "*", "0", "#", "E"};
int totalTeclas = sizeof(mapaTeclas) / sizeof(mapaTeclas[0]);

// Callback de BLE para detectar conexión/desconexión
class MyServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) override {
        deviceConnected = true;
        Serial.println("BLE Conectado!");
        //pServer->updateConnParams(pServer->getConnHandle(), 6, 12, 0, 100);  // Mejora la conexión BLE
    }

    void onDisconnect(BLEServer* pServer) override {
        deviceConnected = false;
        Serial.println("BLE Desconectado!");
    }
};

class MyMTUCallback : public BLEServerCallbacks {
    void onMTUChange(uint16_t mtu){
        Serial.printf("MTU cambiado a: %d bytes\n", mtu);
    }
};

void enviarRegistros() {
    File archivo = SPIFFS.open("/log_teclado.txt", "r");
    if (!archivo) {
        Serial.println("No se pudo abrir el archivo de registros.");
        return;
    }

    String buffer = "";  // Acumula datos hasta 512 bytes
    while (archivo.available()) {
        String linea = archivo.readStringUntil('\n');
        if (buffer.length() + linea.length() >= 512) {
            if (deviceConnected && pTxCharacteristic) {
                pTxCharacteristic->setValue(buffer.c_str());
                pTxCharacteristic->notify();
            }else{
                Serial.println("Error de conexión");
            }
            buffer = "";  // Reiniciar buffer
            delay(50);    // Evitar congestión
        }
        buffer += linea + "\n";
    }

    if (buffer.length() > 0) {  // Enviar el último fragmento
        if (deviceConnected && pTxCharacteristic) {
            pTxCharacteristic->setValue(buffer.c_str());
            pTxCharacteristic->notify();
        }
    }

    archivo.close();
    Serial.println("Envío de registros completado.");
}



class MyCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pCharacteristic) override {
        String rxValue = pCharacteristic->getValue().c_str();
        if (rxValue == "CLEAR") {
            if (SPIFFS.exists("/log_teclado.txt")) {
                SPIFFS.remove("/log_teclado.txt");
            }
            preferences.putInt("indice", 1);
            indice = 1;
            Serial.println("Registros Borrados");
        } else if (rxValue == "GET_LOGS") {
            Serial.println("Solicitud de registros recibida. Enviando...");
            enviarRegistros();
        } else if ((rxValue.length() > 0)) {
            Serial.print("Recibido: ");
            Serial.println(rxValue);

            // Procesar la fecha y hora recibida (Formato esperado: "YYYY-MM-DD HH:MM:SS")
            String data = String(rxValue.c_str());
            int year = data.substring(0, 4).toInt();
            int month = data.substring(5, 7).toInt();
            int day = data.substring(8, 10).toInt();
            int hour = data.substring(11, 13).toInt();
            int minute = data.substring(14, 16).toInt();
            int second = data.substring(17, 19).toInt();

            if (year > 2000) { // Validar que la fecha recibida es correcta
              struct tm t;
              t.tm_year = year - 1900; // Año desde 1900
              t.tm_mon = month - 1;    // Mes (0-11)
              t.tm_mday = day;
              t.tm_hour = hour;
              t.tm_min = minute;
              t.tm_sec = second;
              time_t epochTime = mktime(&t); // Convertir a epoch
              struct timeval now = {epochTime, 0};
              settimeofday(&now, NULL); // Establecer la hora del sistema

              Serial.print("Hora actualizada: ");
              Serial.println(rxValue);
              horaActualizada = true; // Marcar que ya se recibió la hora
            }else{
              Serial.println("Error al actualizar la hora.");
              delay(1000);
            }
        }else{
          Serial.println("Error al recibir datos.");
          delay(1000);
          return;
        }
    }
};

void setup() {
    Serial.begin(115200);
    delay(1000);

    pinMode(LED_BUILTIN, OUTPUT);

    preferences.begin("datos", false);  // Abre un espacio de almacenamiento

    if (!SPIFFS.begin(true)) {
        Serial.println("Error al montar SPIFFS.");
        return;
    }

    // Leer el último registro guardado en el archivo
    File archivo = SPIFFS.open("/log_teclado.txt", "r");
    if (archivo) {
        String ultimaLinea;
        while (archivo.available()) {
            ultimaLinea = archivo.readStringUntil('\n');  // Guarda la última línea leída
        }
        archivo.close();

        if (ultimaLinea.length() > 0) {
            Serial.print("Último registro guardado: ");
            Serial.println(ultimaLinea);

            // Extraer el índice de la última línea
            int espacio1 = ultimaLinea.indexOf(' ');
            if (espacio1 != -1) {
                String indiceStr = ultimaLinea.substring(0, espacio1);
                indice = indiceStr.toInt() + 1;  // Usar el siguiente índice disponible
            }
        }
    } else {
        Serial.println("No se encontró el archivo de registros.");
        indice = 1;  // Si no hay archivo, empezamos desde 1
    }

    // Inicializar BLE
    activarBLE();
    
    // Esperar hasta que se actualice la hora
    unsigned long startTime = millis();
    unsigned long timeout = 30000; // 30 segundos de espera

    while (!horaActualizada) {
        if (deviceConnected) {
            blink(500, 50);
        } else {
            digitalWrite(LED_BUILTIN, LOW);
        }
    }

    // Apagar BLE después de recibir la hora para ahorrar energía
    BLEDevice::deinit();
    Serial.println("BLE desactivado. Continuando con la recolección de datos...");
    horaActualizada = false;

}

void loop() {

  while(!reconect){
    emularTecla();
    blink(2000, 100);
  }

  if(reconect){
    horaActualizada = false;
    while (!horaActualizada) {
        if (deviceConnected) {
            blink(500, 50);
        } else {
            digitalWrite(LED_BUILTIN, LOW);
        }
    }

    // Apagar BLE después de recibir la hora para ahorrar energía
    BLEDevice::deinit();
    Serial.println("BLE desactivado. Continuando con la recolección de datos...");

  reconect = false;
  delay(100);
  }

}

void emularTecla() {

    //String tecla = mapaTeclas[random(totalTeclas)];
    for(int i=0;i<40;i++){
      for(int i=0;i<totalTeclas;i++){
        String tecla = mapaTeclas[i];
        guardarTecla(tecla);
        delay(50);
      }
    }
    String tecla = "1";
    guardarTecla(tecla);
    delay(100);
    tecla = "2";
    guardarTecla(tecla);
    delay(100); 
    tecla = "3";
    guardarTecla(tecla);
    delay(100); 
    tecla = "4";
    guardarTecla(tecla);
    delay(100); 
}

void blink(int dly, int bnk) {
    if (dly < bnk){dly = bnk;}
    digitalWrite(LED_BUILTIN, LOW);  
    delay(bnk);                      
    digitalWrite(LED_BUILTIN, HIGH);   
    delay((dly-bnk)); 
}

void guardarTecla(String tecla) {
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    char fechaHora[30];
    int centesimas = (millis() % 1000) / 10;
    sprintf(fechaHora, "%04d-%02d-%02d %02d:%02d:%02d:%02d", 
            timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday, 
            timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, centesimas);

    File archivo = SPIFFS.open("/log_teclado.txt", "a");
    if (!archivo) {
        Serial.println("Error al abrir el archivo.");
        return;
    }
    archivo.printf("%d %s %s\n", indice, tecla.c_str(), fechaHora);
    archivo.close();

    Serial.printf("Guardado: %d %s %s\n", indice, tecla.c_str(), fechaHora);

    // Guardar índice en la memoria flash para que no se pierda tras reinicio
    preferences.putInt("indice", indice);
    indice++;

    // Verificar si la secuencia ingresada coincide con la clave secreta
    verificarClave(tecla);
}

void verificarClave(String tecla) {
    secuenciaIngresada += tecla; // Agregar la nueva tecla presionada

    // Mantener solo los últimos caracteres de la clave secreta
    if (secuenciaIngresada.length() > claveSecreta.length()) {
        secuenciaIngresada = secuenciaIngresada.substring(secuenciaIngresada.length() - claveSecreta.length());
    }

    // Verificar si coincide con la clave secreta
    if (secuenciaIngresada == claveSecreta) {
        Serial.println("Clave Recibida, activando BLE...");
        activarBLE();
        secuenciaIngresada = ""; // Resetear la secuencia después de activarlo
        reconect = true;
    }
}

void activarBLE() {
    BLEDevice::init("ESP32_BLE");
    pServer = BLEDevice::createServer();

    pServer->setCallbacks(new MyMTUCallback());
    BLEDevice::setMTU(512);  // Solicita un tamaño de paquete más grande

    pServer->setCallbacks(new MyServerCallbacks());

    BLEService* pService = pServer->createService(SERVICE_UUID);

    // Característica para recibir datos
    pRxCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID_RX,
        BLECharacteristic::PROPERTY_WRITE
    );
    pRxCharacteristic->setCallbacks(new MyCallbacks());

    // Característica para enviar datos (TX)
    pTxCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID_TX,
        BLECharacteristic::PROPERTY_NOTIFY
    );

    BLE2902* desc = new BLE2902();
    desc->setNotifications(true);
    pTxCharacteristic->addDescriptor(desc);

    pService->start();
    BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);
    pAdvertising->setMinPreferred(0x12);
    BLEDevice::startAdvertising();

    Serial.println("BLE activado y esperando conexión...");
}


