/*
  Based on Neil Kolban example for IDF: https://github.com/nkolban/esp32-snippets/blob/master/cpp_utils/tests/BLE%20Tests/SampleNotify.cpp
  Ported to Arduino ESP32 by Evandro Copercini
  updated by chegewara and MoThunderz
*/
#include <NimBLEDevice.h>

NimBLEServer* pServer = nullptr;
NimBLECharacteristic* pCharacteristic = nullptr;
NimBLECharacteristic* pCharacteristic_2 = nullptr;

bool deviceConnected = false;
bool oldDeviceConnected = false;
uint32_t value = 0;

// UUIDs
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHAR1_UUID          "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define CHAR2_UUID          "e3223119-9445-4e96-a4a1-85358c4046a2"

class MyServerCallbacks: public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
        deviceConnected = true;
        Serial.println("Cliente conectado!");
    };

    void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
        deviceConnected = false;
        Serial.println("Cliente desconectado!");
    }
};

class CharacteristicCallBack: public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pChar, NimBLEConnInfo& connInfo) override {
        std::string rxValue = pChar->getValue();
        if (!rxValue.empty()) {
            String valueStr = String(rxValue.c_str());
            int intValue = valueStr.toInt();
            Serial.print("Char2 escrito: ");
            Serial.println(intValue);
        }
    }
};

void setup() {
    Serial.begin(115200);
    Serial.println("Iniciando NimBLE...");

    // Inicializar NimBLE
    NimBLEDevice::init("ESP32-NimBLE");
    NimBLEDevice::setPower(ESP_PWR_LVL_P7); // potencia máxima
    NimBLEDevice::setSecurityAuth(true, true, true);

    // Crear servidor
    pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    // Crear servicio
    NimBLEService* pService = pServer->createService(SERVICE_UUID);

    // Característica de notificación
    pCharacteristic = pService->createCharacteristic(
        CHAR1_UUID,
        NIMBLE_PROPERTY::NOTIFY
    );

    // Característica de lectura/escritura
    pCharacteristic_2 = pService->createCharacteristic(
        CHAR2_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE
    );
    pCharacteristic_2->setCallbacks(new CharacteristicCallBack());

    // Iniciar servicio
    pService->start();
    Serial.println("Servicio iniciado!");

    // Publicar
    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->start();

    Serial.println("Esperando conexión de cliente BLE...");
    Serial.println("Esperando conexión de cliente BLE...");
}

void loop() {
    if (deviceConnected) {
        Serial.print("Enviando notificación con valor: ");
        Serial.println(value);
        pCharacteristic->setValue((uint32_t)value);
        pCharacteristic->notify();
        value++;
        delay(1000);
    }

    // Si se desconecta
    if (!deviceConnected && oldDeviceConnected) {
        delay(500); // darle chance al stack
        Serial.println("Reiniciando advertising...");
        NimBLEDevice::startAdvertising();
        oldDeviceConnected = deviceConnected;
    }

    // Si se conecta
    if (deviceConnected && !oldDeviceConnected) {
        Serial.println("Nuevo cliente conectado!");
        oldDeviceConnected = deviceConnected;
    }
}
