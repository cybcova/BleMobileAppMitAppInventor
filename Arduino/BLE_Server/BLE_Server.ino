/*
  Based on Neil Kolban example for IDF: https://github.com/nkolban/esp32-snippets/blob/master/cpp_utils/tests/BLE%20Tests/SampleNotify.cpp
  Ported to Arduino ESP32 by Evandro Copercini
  updated by chegewara and MoThunderz
*/
//WIFI
#include <WiFi.h>
#include <WiFiClientSecure.h>

//BL
#include <NimBLEDevice.h>

//RFID
#include <SPI.h>
#include <MFRC522.h>

//Http
#include <HTTPClient.h>

const char* WIFI_SSID = "Totalplay-";
const char* WIFI_PASS = "B5A8";
const char* SUPA_URL  = "https://.supabase.co/rest/v1/recorrido_puntos";
const char* SUPA_ANON = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9";

// -----------------------------------

NimBLEServer* pServer = nullptr;
NimBLECharacteristic* pCharacteristic = nullptr;
NimBLECharacteristic* pCharacteristic_2 = nullptr;

bool deviceConnected = false;
bool oldDeviceConnected = false;

volatile bool g_hasNew = false;
String g_lastLatLon;

//Tiempo Led
const long intervaloLed = 500;  // milisegundos
unsigned long anteriorMillisRojo = 0;
unsigned long anteriorMillisVerde = 0;


//Estado Led
bool estadoVerde = false;
bool estadoRojo = true;
bool estadoAzul = true;

// UUIDs
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHAR1_UUID          "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define CHAR2_UUID          "e3223119-9445-4e96-a4a1-85358c4046a2"

//Leds
#define PIN_R 25
#define PIN_G 27
#define PIN_B 32

//RFID
#define SS_PIN 21   // SDA
#define RST_PIN 22  // RST

MFRC522 mfrc522(SS_PIN, RST_PIN);

///////////////////////////////////////// ---- Conexion Ble ---- ///////////////////////////////////////// 
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

///////////////////////////////////////// ---- Conexion Wifi ---- ///////////////////////////////////////// 
void wifiConnect() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("[WiFi] Conectando");
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) {
    delay(400);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[WiFi] OK. IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\n[WiFi] No conectado (timeout). Reintentará en loop.");
  }
}

///////////////////////////////////////// ---- Recibir valores Ble ---- ///////////////////////////////////////// 
class CharacteristicCallBack: public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* pChar, NimBLEConnInfo& connInfo) override {
    std::string rxValue = pChar->getValue();
    if (!rxValue.empty()) {
      g_lastLatLon = String(rxValue.c_str());
      g_hasNew = true;
      Serial.println("valueStr (buffered): " + g_lastLatLon);
    }
  }
};


///////////////////////////////////////// ---- POST a Supabase ---- ///////////////////////////////////////// 
bool postToSupabase(String latLon) {
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  if (!http.begin(client, SUPA_URL)) {
    Serial.println("[HTTP] begin() falló");
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  http.addHeader("apikey", SUPA_ANON);
  http.addHeader("Authorization", String("Bearer ") + SUPA_ANON);
  http.addHeader("Prefer", "return=representation");

  String payload = String("{\"camion_id\": 1, " 
    "\"coo\":\"") + latLon + String("\"}"); 
  
  Serial.println("[JSON]: " + payload);

  int code = http.POST(payload);
  Serial.printf("[HTTP] POST code: %d\n", code);
  String resp = http.getString();
  Serial.println("[HTTP] Resp: " + resp);
  http.end();

  return code >= 200 && code < 300;
}


void setup() {
    Serial.begin(115200);

    //Leds    
    pinMode(PIN_R, OUTPUT);
    pinMode(PIN_G, OUTPUT);
    pinMode(PIN_B, OUTPUT);
    
    digitalWrite(PIN_B, true);

    //RFID
    SPI.begin(18, 19, 23, 21);  // SCK, MISO, MOSI, SS
    mfrc522.PCD_Init();
    Serial.println("Acerque una tarjeta RFID...");


    Serial.println("Iniciando NimBLE...");

    wifiConnect();

    // Inicializar NimBLE
    NimBLEDevice::init("ESP32");
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


}

void loop() {
  
    unsigned long actualMillis = millis();
  
    // Re-conexión Wi-Fi si se cayó
    static unsigned long lastWifiCheck = 0;
    if (millis() - lastWifiCheck > 5000) {
            lastWifiCheck = millis();
            if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[WiFi] Reintentando conexión...");
            wifiConnect();
        }
    }
    
    // Si llegó dato BLE, postéar
    if (g_hasNew) {
        String latLon = g_lastLatLon;   // copia local (no volatile)
        g_hasNew = false;
        bool ok = postToSupabase(latLon);
        Serial.printf("[POST] %s %s\n", ok ? "OK" : "FAIL", latLon.c_str());
        
      //Prender Led Verde
      estadoVerde = true;
      digitalWrite(PIN_G, estadoVerde);
      anteriorMillisVerde = millis();
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

    //Prueba RFID
    if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
      String uidStr = "";

      // Construye el UID como cadena hexadecimal
      for (byte i = 0; i < mfrc522.uid.size; i++) {
        if (mfrc522.uid.uidByte[i] < 0x10) uidStr += "0";
        uidStr += String(mfrc522.uid.uidByte[i], HEX);
      }

      uidStr.toUpperCase(); // opcional: convierte a mayúsculas

      Serial.print("UID detectado: ");
      Serial.println(uidStr);

      if (deviceConnected) {
        Serial.print("Enviando notificación con valor: ");
        Serial.println(uidStr);

        // Enviar el UID como string por BLE
        pCharacteristic->setValue(uidStr.c_str());
        pCharacteristic->notify();
      }

      //Prender Led Rojo
      estadoRojo = true;
      digitalWrite(PIN_R, estadoRojo);
      anteriorMillisRojo = millis();
    }

    //Reset Led
    if(estadoRojo && millis() - anteriorMillisRojo >= intervaloLed){
      estadoRojo = false;
      digitalWrite(PIN_R, estadoRojo);
    }
    if(estadoVerde && millis() - anteriorMillisVerde >= intervaloLed){
      estadoVerde = false;
      digitalWrite(PIN_G, estadoVerde);
    }
    delay(200); // pequeño respiro; evita hoggear el CPU
}
