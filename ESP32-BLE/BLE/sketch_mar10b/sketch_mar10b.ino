#include <Arduino.h>
#include <SPIFFS.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <time.h>  // Librería para manejar el reloj interno
#include <Preferences.h>  // Biblioteca para guardar datos en la flash
#include <BLE2902.h>
#include "esp_gap_ble_api.h"  // Necesario para modificar parámetros BLE

Preferences preferences;  // Objeto para manejar memoria flash

String secuenciaIngresada = ""; // Almacena la secuencia de teclas ingresadas
const String claveSecreta = "1234"; // Define la clave secreta para activar BLE

// BLE UUIDs
#define SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"

BLEServer* pServer = nullptr;

BLECharacteristic* pCharacteristic;
bool deviceConnected = false;
bool horaActualizada = false;
bool reconect = false;

File archivo;

int indice = 1;

String buffer = "";  // Acumula datos hasta 512 bytes

String mapaTeclas[] = {"F1", "F2", "F3", "F4", "1", "2", "3", "A", "4", "5", "6", "B", "7", "8", "9", "C", "*", "0", "#", "E"};
int totalTeclas = sizeof(mapaTeclas) / sizeof(mapaTeclas[0]);

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
    archivo = SPIFFS.open("/log_teclado.txt", "r");
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


    while (!horaActualizada) {
        if (deviceConnected) {
            blink(500, 50);
        } else {
            digitalWrite(LED_BUILTIN, LOW);
            BLEDevice::startAdvertising();
            blink(5000, 2500);
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
  }

  if(reconect){
    horaActualizada = false;
    while (!horaActualizada) {
        if (deviceConnected) {
            blink(500, 50);
        } else {
            digitalWrite(LED_BUILTIN, LOW);
            BLEDevice::startAdvertising();
            blink(5000, 2500);
        }
    }

  // Apagar BLE después de recibir la hora para ahorrar energía

  BLEDevice::deinit();
  Serial.println("BLE desactivado. Continuando con la recolección de datos...");

  reconect = false;
  delay(50);
  }

}

// Callback de BLE para detectar conexión/desconexión
class MyServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer, esp_ble_gatts_cb_param_t *param) override {
        deviceConnected = true;
        Serial.println("BLE Conectado!");
        // Establecer nuevos parámetros de conexión BLE
        esp_ble_conn_update_params_t conn_params = {};
        memcpy(conn_params.bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
        conn_params.min_int = 6;    // Intervalo mínimo (7.5ms * 6 = 45ms)
        conn_params.max_int = 12;   // Intervalo máximo (7.5ms * 12 = 90ms)
        conn_params.latency = 0;    // Sin latencia
        conn_params.timeout = 100;  // Tiempo de espera (100 * 10ms = 1s)

        // Aplicar los nuevos parámetros
        esp_ble_gap_update_conn_params(&conn_params);

        archivo = SPIFFS.open("/log_teclado.txt", "r");
        if (!archivo) {
          Serial.println("No se pudo abrir el archivo de registros.");
        return;
        }else{
          Serial.println("Archivo abierto.");
        }
    }

    void onDisconnect(BLEServer* pServer) override {
        deviceConnected = false;
        Serial.println("BLE Desconectado!");
        archivo.close();
        Serial.println("Archivo cerrado.");
        delay(100);
    }
};

class MyMTUCallback : public BLEServerCallbacks {
    void onMTUChange(uint16_t mtu){
        Serial.printf("MTU cambiado a: %d bytes\n", mtu);
    }
};

void enviarRegistros() {
    bool paquete = true;
    while (archivo.available() && paquete) {
          String linea = archivo.readStringUntil('\n');
          if (buffer.length() + linea.length() >= 480) {
              if (deviceConnected && pCharacteristic) {
                  Serial.println("Enviando paquete...");
                  pCharacteristic->setValue(buffer.c_str());
                  pCharacteristic->notify();
                  paquete = false;
              }else{
                  Serial.println("Error de conexión");
              }
          buffer = "";  // Reiniciar buffer
          buffer += linea + "\n";
          }else{
            buffer += linea + "\n";
          }
    }

    if ((buffer.length() > 0) && paquete) {  // Enviar el último fragmento
        if (deviceConnected && pCharacteristic) {
            pCharacteristic->setValue(buffer.c_str());
            pCharacteristic->notify();
            Serial.println("Envío de registros completado.");
            buffer = "";  // Reiniciar buffer
            archivo.close();
            Serial.println("Archivo cerrado.");
            archivo = SPIFFS.open("/log_teclado.txt", "r");
            if (!archivo) {
            Serial.println("No se pudo abrir el archivo de registros.");
            return;
            }else{
          Serial.println("Archivo abierto.");
        }
            delay(100);
            pCharacteristic->setValue("FINISH_SEND");
            pCharacteristic->notify();

        }
    }else{
        Serial.println("Paquete enviado.\nEsperando otra Solicitud de descarga...");
        delay(100);
        pCharacteristic->setValue("NO_FINISH_SEND");
        pCharacteristic->notify();
    }
}



class MyCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pCharacteristic){
        String rxValue = pCharacteristic->getValue();
        if (rxValue == "CLEAR") {
            Serial.println("Solicitud de borrado recibida.\nBorrando...");
            if (SPIFFS.exists("/log_teclado.txt")) {
              SPIFFS.remove("/log_teclado.txt");
              preferences.putInt("indice", 1);
              indice = 1;
              Serial.println("Registros Borrados");
              delay(100);
              pCharacteristic->setValue("FINISH_CLEAR");
              pCharacteristic->notify();
            }else{
              pCharacteristic->setValue("ERROR_CLEAR");
              pCharacteristic->notify();  
            }
        } else if (rxValue == "GET_LOGS") {
            Serial.println("Solicitud de descarga recibida.");
            enviarRegistros();
        } else if ((rxValue.length() > 0)) {
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
              pCharacteristic->setValue("FINISH_DATE");
              pCharacteristic->notify();
            }else{
              Serial.println("Error al actualizar la hora.");
              delay(100);
              pCharacteristic->setValue("ERROR_DATE");
              pCharacteristic->notify();
            }
        }else{
          Serial.println("Error al recibir datos.");
          delay(100);
          pCharacteristic->setValue("ERROR_MESSAGE");
          pCharacteristic->notify();
          return;
        }
    }
};

void emularTecla() {

    //String tecla = mapaTeclas[random(totalTeclas)];
    for(int i=0;i<10;i++){
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

    archivo = SPIFFS.open("/log_teclado.txt", "a");
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

    BLEDevice::setMTU(512);  // Solicita un tamaño de paquete más grande

    pServer->setCallbacks(new MyMTUCallback());
    pServer->setCallbacks(new MyServerCallbacks());

    BLEService* pService = pServer->createService(SERVICE_UUID);

    // Característica para recibir datos
    pCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID,
        BLECharacteristic::PROPERTY_READ |
        BLECharacteristic::PROPERTY_WRITE |
        BLECharacteristic::PROPERTY_NOTIFY
    );

    pCharacteristic->setCallbacks(new MyCallbacks());

    BLE2902* desc = new BLE2902();
    desc->setNotifications(true);
    pCharacteristic->addDescriptor(desc);

    pService->start();
    BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);
    pAdvertising->setMinPreferred(0x12);
    BLEDevice::startAdvertising();

    Serial.println("BLE activado y esperando conexión...");
}


