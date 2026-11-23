#include <Arduino.h>
#include <WiFi.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "LittleFS.h"

// BLE UUIDs - you can generate your own, but these are standard test ones
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

// Variables to save values
String ssid = "";
String pass = "";

// File paths in LittleFS
const char* ssidPath = "/ssid.txt";
const char* passPath = "/pass.txt";

// BLE server status
bool deviceConnected = false;
BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;

// LittleFS utilities
void initLittleFS() {
  if (!LittleFS.begin(true)) {
    Serial.println("An error has occurred while mounting LittleFS");
    return;
  }
  Serial.println("LittleFS mounted successfully");
}

String readFile(const char * path) {
  File file = LittleFS.open(path, "r");
  if (!file || file.isDirectory()) {
    return "";
  }
  String content = "";
  while (file.available()) {
    content += (char)file.read();
  }
  file.close();
  content.trim(); // remove newlines/spaces
  return content;
}

void writeFile(const char * path, const char * message) {
  File file = LittleFS.open(path, "w");
  if (!file) {
    Serial.println("Failed to open file for writing");
    return;
  }
  if (file.print(message)) {
    Serial.println("File written successfully");
  } else {
    Serial.println("Write failed");
  }
  file.close();
}

// WiFi connection utility
bool connectWiFi() {
  if (ssid == "") {
    Serial.println("No SSID saved in memory.");
    return false;
  }

  Serial.print("Connecting to Wi-Fi: ");
  Serial.println(ssid);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());

  unsigned long startAttemptTime = millis();
  const unsigned long connectionTimeout = 10000; // 10 seconds

  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < connectionTimeout) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nSuccessfully connected to Wi-Fi!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    return true;
  } else {
    Serial.println("\nConnection timed out.");
    return false;
  }
}

// BLE Callback to handle credentials written from the mobile app
class MyBLECallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pChar) {
      String rxValue = String(pChar->getValue().c_str());
      if (rxValue.length() > 0) {
        String data = rxValue;
        data.trim();
        Serial.print("Received BLE Data: ");
        Serial.println(data);

        // Expecting data in format: "SSID,PASSWORD"
        int commaIndex = data.indexOf(',');
        if (commaIndex > 0) {
          String newSSID = data.substring(0, commaIndex);
          String newPass = data.substring(commaIndex + 1);
          newSSID.trim();
          newPass.trim();

          Serial.println("Parsed credentials successfully:");
          Serial.print("SSID: ");
          Serial.println(newSSID);
          Serial.print("Password: ");
          Serial.println("********");

          // Save to LittleFS
          writeFile(ssidPath, newSSID.c_str());
          writeFile(passPath, newPass.c_str());

          Serial.println("Credentials saved. Restarting ESP32 in 2 seconds...");
          delay(2000);
          ESP.restart();
        } else {
          Serial.println("Error: Invalid data format. Must be 'SSID,PASSWORD'");
        }
      }
    }
};

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      Serial.println("Mobile app connected via Bluetooth.");
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      Serial.println("Mobile app disconnected. Restarting advertising...");
      delay(500); // give the bluetooth stack the chance to get ready
      pServer->startAdvertising(); // restart advertising
    }
};

void startBLEProvisioning() {
  Serial.println("Starting BLE Provisioning Server...");
  
  // Initialize BLE Device
  BLEDevice::init("ZONE-ESP32");

  // Create BLE Server
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // Create BLE Service
  BLEService *pService = pServer->createService(SERVICE_UUID);

  // Create BLE Characteristic with WRITE property
  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_READ |
                      BLECharacteristic::PROPERTY_WRITE |
                      BLECharacteristic::PROPERTY_NOTIFY
                    );

  // Set write callbacks
  pCharacteristic->setCallbacks(new MyBLECallbacks());
  pCharacteristic->addDescriptor(new BLE2902());

  // Start Service
  pService->start();

  // Start Advertising
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);  // functions that help with iPhone connections
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();
  
  Serial.println("Advertising started. Open nRF Connect on your phone and connect to 'ZONE-ESP32'.");
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n--- ZONE ESP32 BLE SETUP TEST ---");

  initLittleFS();

  // Load saved credentials
  ssid = readFile(ssidPath);
  pass = readFile(passPath);

  // Try to connect to Wi-Fi
  if (!connectWiFi()) {
    // If connection fails or no credentials saved, launch Bluetooth provisioning
    startBLEProvisioning();
  }
}

void loop() {
  // Loop stays empty as setup manages state changes or reboots
}
