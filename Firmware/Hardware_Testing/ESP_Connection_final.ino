/**
 * Z.O.N.E. Device Telemetry & Handshake Firmware (with BLE Provisioning)
 * 
 * Hardware: ESP32 + Analog Microphone
 * Libraries Required:
 *  - Firebase-ESP-Client (by Mobizt)
 *  - WiFi (standard ESP32 WiFi library)
 * 
 * Flow:
 * 1. Boot up, read MAC address.
 * 2. Mount LittleFS and read saved Wi-Fi credentials.
 * 3. Attempt Wi-Fi connection. If credentials are missing or connection fails, 
 *    start BLE server ("ZONE-ESP32") and wait for credentials.
 * 4. Once credentials are received via BLE, save them to LittleFS and reboot.
 * 5. Handshake: Read "/devices/{MAC}" node to check if paired and get ownerUid & zoneId.
 * 6. Heartbeat: Set up onDisconnect() to automatically flip "isOnline" to false on power loss/disconnect.
 * 7. Stream: Setup real-time listener for "threshold" and "isEnabled" variables.
 * 8. Telemetry: Monitor analog decibel levels and write violation entries to Firebase when threshold is exceeded.
 */
#include <Arduino.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "LittleFS.h"
#include <esp_mac.h>
// Provide the token generation and SDK helper processes
#include <addons/TokenHelper.h>
#include <addons/RTDBHelper.h>
// --- Firebase Configuration ---
#define FIREBASE_API_KEY "AIzaSyBSDsX8rkYPWMBzlHU2tioMeiJ28W4MESs"
#define FIREBASE_DATABASE_URL "https://zone-c0177-default-rtdb.asia-southeast1.firebasedatabase.app"
// --- BLE UUIDs ---
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
// --- Hardware Pins ---
#define MIC_ANALOG_PIN 34 // ADC1 channel 6 on ESP32
// --- Global States ---
String ssid = "";
String pass = "";
String deviceMac = "";
String ownerUid = "";
String zoneId = "";
bool isPaired = false;
bool isEnabled = true;
int soundThreshold = 65; // default decibel/sound threshold
bool wifiDisconnect = false;
bool wifiForget = false;
// File paths in LittleFS
const char* ssidPath = "/ssid.txt";
const char* passPath = "/pass.txt";
// BLE server status
bool deviceConnected = false;
BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;
// Firebase Data Objects
FirebaseData fbdoHandshake;
FirebaseData fbdoStream;
FirebaseData fbdoWrite;
FirebaseAuth auth;
FirebaseConfig config;
// Helpers
String getFormattedMacAddress();
void performDatabaseHandshake();
void startRealtimeStream();
void streamCallback(FirebaseStream data);
void streamTimeoutCallback(bool timeout);
void monitorSound();
void initLittleFS();
String readFile(const char * path);
void writeFile(const char * path, const char * message);
bool connectWiFi();
void startBLEProvisioning();
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
  content.trim();
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
// BLE Callbacks
class MyBLECallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pChar) {
      String rxValue = String(pChar->getValue().c_str());
      if (rxValue.length() > 0) {
        String data = rxValue;
        data.trim();
        Serial.print("Received BLE Data: ");
        Serial.println(data);
        if (data == "CONNECT_SAVED") {
          Serial.println("BLE command: CONNECT_SAVED. Restarting to connect...");
          delay(1000);
          ESP.restart();
        } 
        else if (data == "FORGET") {
          Serial.println("BLE command: FORGET. Clearing credentials and restarting...");
          writeFile(ssidPath, "");
          writeFile(passPath, "");
          delay(1000);
          ESP.restart();
        } 
        else {
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
            Serial.println("Error: Invalid data format. Must be 'SSID,PASSWORD' or CONNECT_SAVED/FORGET");
          }
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
      delay(500);
      pServer->startAdvertising();
    }
};
void startBLEProvisioning() {
  static bool bleInitialized = false;
  if (bleInitialized) {
    Serial.println("BLE Provisioning already initialized. Starting advertising...");
    BLEDevice::startAdvertising();
    return;
  }
  bleInitialized = true;
  Serial.println("Starting BLE Provisioning Server...");
  BLEDevice::init("ZONE-ESP32");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  BLEService *pService = pServer->createService(SERVICE_UUID);
  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_READ |
                      BLECharacteristic::PROPERTY_WRITE |
                      BLECharacteristic::PROPERTY_NOTIFY
                    );
  pCharacteristic->setCallbacks(new MyBLECallbacks());
  pCharacteristic->addDescriptor(new BLE2902());
  // Set initial value so client can read if there is a saved SSID
  if (ssid.length() > 0) {
    pCharacteristic->setValue(("SSID:" + ssid).c_str());
  } else {
    pCharacteristic->setValue("NONE");
  }
  pService->start();
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();
  
  Serial.println("Advertising started. Connect using your ZONE app or LightBlue.");
}
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n--- Z.O.N.E. Device Booting ---");
  // 1. Initialize WiFi interface in STA mode so we can read the correct MAC address
  WiFi.mode(WIFI_STA);
  delay(100);
  // Get MAC Address & Format it (all caps, no colons/dashes)
  deviceMac = getFormattedMacAddress();
  Serial.print("MAC Address: ");
  Serial.println(deviceMac);
  initLittleFS();
  // 2. Load Wi-Fi credentials
  ssid = readFile(ssidPath);
  pass = readFile(passPath);
  // 3. Connect to WiFi
  if (!connectWiFi()) {
    // If connection fails, launch Bluetooth provisioning and halt execution
    startBLEProvisioning();
    while (true) {
      delay(1000);
    }
  }
  // 4. Configure Firebase
  config.api_key = FIREBASE_API_KEY;
  config.database_url = FIREBASE_DATABASE_URL;
  
  // Assign the token status callback from TokenHelper.h to print status/errors
  config.token_status_callback = tokenStatusCallback;
  
  auth.user.email = ""; 
  auth.user.password = "";
  // Set buffer sizes to conserve memory and avoid "closed SSL connection" failures
  fbdoHandshake.setBSSLBufferSize(4096, 1024);
  fbdoHandshake.setResponseSize(4096);
  fbdoStream.setBSSLBufferSize(8192, 1024);
  fbdoStream.setResponseSize(8192);
  fbdoWrite.setBSSLBufferSize(4096, 1024);
  fbdoWrite.setResponseSize(4096);
  Serial.println("Signing up anonymously...");
  if (Firebase.signUp(&config, &auth, "", "")) {
    Serial.println("Anonymous login/registration initiated successfully.");
  } else {
    Serial.printf("Signup error: %s\n", config.signer.signupError.message.c_str());
  }
  config.timeout.socketConnection = 30 * 1000;
  config.timeout.serverResponse = 10 * 1000;
  config.timeout.rtdbKeepAlive = 45 * 1000;
  config.timeout.rtdbStreamReconnect = 1000;
  Firebase.reconnectWiFi(true);
  Firebase.begin(&config, &auth);
}
void loop() {
  if (wifiDisconnect) {
    Serial.println("wifiControl: disconnect command received.");
    
    // Clear command in Firebase first
    String devicePath = "/users/" + ownerUid + "/zones/" + zoneId + "/devices/" + deviceMac;
    Firebase.RTDB.setString(&fbdoWrite, (devicePath + "/wifiControl").c_str(), "");
    delay(200);
    Firebase.RTDB.setBool(&fbdoWrite, (devicePath + "/isOnline").c_str(), false);
    
    delay(500);
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    
    Serial.println("Wi-Fi disconnected. Starting BLE provisioning...");
    startBLEProvisioning();
    while (true) {
      delay(1000);
    }
  }
  if (wifiForget) {
    Serial.println("wifiControl: forget command received.");
    
    // Clear command in Firebase
    String devicePath = "/users/" + ownerUid + "/zones/" + zoneId + "/devices/" + deviceMac;
    Firebase.RTDB.setString(&fbdoWrite, (devicePath + "/wifiControl").c_str(), "");
    delay(200);
    Firebase.RTDB.setBool(&fbdoWrite, (devicePath + "/isOnline").c_str(), false);
    
    delay(500);
    writeFile(ssidPath, "");
    writeFile(passPath, "");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    
    Serial.println("Credentials forgotten. Restarting...");
    delay(1000);
    ESP.restart();
  }
  if (Firebase.ready()) {
    if (!isPaired) {
      // Start BLE provisioning in the background so the user can configure/forget even if not paired
      static bool bleStartedUnpaired = false;
      if (!bleStartedUnpaired) {
        Serial.println("Device not paired yet. Starting background BLE advertising for setup/recovery...");
        startBLEProvisioning();
        bleStartedUnpaired = true;
      }
      performDatabaseHandshake();
      if (!isPaired) {
        // Wait 10 seconds before retrying if handshake failed (e.g. not registered in dashboard)
        delay(10000);
      }
    } else {
      // Stream Watchdog: Check if the stream connection has dropped
      static unsigned long lastStreamCheck = 0;
      if (millis() - lastStreamCheck > 5000) {
        lastStreamCheck = millis();
        if (!fbdoStream.httpConnected()) {
          Serial.println("Stream connection lost! Re-establishing stream...");
          Firebase.RTDB.endStream(&fbdoStream);
          delay(100);
          startRealtimeStream();
        }
      }
      // Heartbeat: Write server timestamp to lastSeen every 5 seconds
      static unsigned long lastHeartbeat = 0;
      if (millis() - lastHeartbeat > 5000) {
        lastHeartbeat = millis();
        String devicePath = "/users/" + ownerUid + "/zones/" + zoneId + "/devices/" + deviceMac;
        if (Firebase.RTDB.setTimestamp(&fbdoWrite, (devicePath + "/lastSeen").c_str())) {
          Serial.println("Heartbeat sent successfully.");
        } else {
          Serial.printf("Heartbeat failed! Reason: %s\n", fbdoWrite.errorReason().c_str());
        }
      }
      if (isEnabled) {
        monitorSound();
      }
      delay(100); // Small loop delay
    }
  } else {
    // If not ready, wait a bit and print status
    static unsigned long lastReadyMsg = 0;
    static unsigned long wifiConnectedTime = 0;
    if (wifiConnectedTime == 0) {
      wifiConnectedTime = millis();
    }
    if (millis() - lastReadyMsg > 5000) {
      Serial.println("Waiting for Firebase session/tokens to become ready...");
      lastReadyMsg = millis();
    }
    // If we have been waiting for Firebase tokens for more than 30 seconds,
    // start BLE provisioning in the background so the user can recover (e.g. Forget network)
    if (millis() - wifiConnectedTime > 30000) {
      static bool bleStartedOnTimeout = false;
      if (!bleStartedOnTimeout) {
        Serial.println("Firebase connection timeout (30s). Starting background BLE advertising for recovery...");
        startBLEProvisioning();
        bleStartedOnTimeout = true;
      }
    }
    delay(500);
  }
}
// Format Wi-Fi MAC Address to match database keys: uppercase without colons
String getFormattedMacAddress() {
  uint8_t mac[6];
  if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
    char macStr[13];
    snprintf(macStr, sizeof(macStr), "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(macStr);
  }
  
  // Fallback to old method if esp_read_mac fails
  String rawMac = WiFi.macAddress();
  rawMac.replace(":", "");
  rawMac.toUpperCase();
  return rawMac;
}
// Handshake with registry: retrieve ownerUid and zoneId
void performDatabaseHandshake() {
  Serial.println("Performing registry handshake...");
  String path = "/devices/" + deviceMac;
  
  if (Firebase.RTDB.getJSON(&fbdoHandshake, path.c_str())) {
    FirebaseJson &json = fbdoHandshake.jsonObject();
    FirebaseJsonData jsonData;
    
    // Check if paired
    json.get(jsonData, "paired");
    if (jsonData.success && jsonData.typeNum == FirebaseJson::JSON_BOOL && jsonData.boolValue == true) {
      
      // Get ownerUid
      json.get(jsonData, "ownerUid");
      if (jsonData.success && jsonData.typeNum == FirebaseJson::JSON_STRING) {
        ownerUid = jsonData.stringValue;
      }
      
      // Get zoneId
      json.get(jsonData, "zoneId");
      if (jsonData.success && jsonData.typeNum == FirebaseJson::JSON_STRING) {
        zoneId = jsonData.stringValue;
        isPaired = true;
        
        Serial.println("====================================");
        Serial.println("Pairing Status: REGISTERED & PAIRED!");
        Serial.print("Owner UID: "); Serial.println(ownerUid);
        Serial.print("Zone ID: "); Serial.println(zoneId);
        Serial.println("====================================");
        
        // Setup OnDisconnect and Start Streaming Configs
        startRealtimeStream();
      }
    } else {
      isPaired = false;
      Serial.println("Pairing Status: UNPAIRED. Please pair this device via your Z.O.N.E. dashboard.");
    }
  } else {
    Serial.print("Handshake failed. Reason: ");
    Serial.println(fbdoHandshake.errorReason());
  }
}
// Setup live configuration listener and physical status heartbeats
void startRealtimeStream() {
  String devicePath = "/users/" + ownerUid + "/zones/" + zoneId + "/devices/" + deviceMac;
  
  // 1. Set online heartbeat status
  Serial.print("Attempting to write isOnline=true to path: ");
  Serial.println((devicePath + "/isOnline").c_str());
  
  if (Firebase.RTDB.setBool(&fbdoWrite, (devicePath + "/isOnline").c_str(), true)) {
    Serial.println("Successfully set isOnline to true!");
  } else {
    Serial.print("Failed to set isOnline. Reason: ");
    Serial.println(fbdoWrite.errorReason());
  }
  
  // 2. Clear any pending wifiControl commands in the database to prevent immediate reconnection loops
  Firebase.RTDB.setString(&fbdoWrite, (devicePath + "/wifiControl").c_str(), "");
  
  // 3. Write the current SSID to the database so the dashboard knows it has a saved network
  Firebase.RTDB.setString(&fbdoWrite, (devicePath + "/savedSsid").c_str(), ssid);
  
  // 4. Start streaming listener for threshold and active toggles
  Serial.print("Starting stream listener at path: ");
  Serial.println(devicePath.c_str());
  
  if (!Firebase.RTDB.beginStream(&fbdoStream, devicePath.c_str())) {
    Serial.print("Stream failed. Reason: ");
    Serial.println(fbdoStream.errorReason());
  } else {
    fbdoStream.keepAlive(5, 5, 1);
    Firebase.RTDB.setStreamCallback(&fbdoStream, streamCallback, streamTimeoutCallback);
    Serial.println("Subscribed to live configuration stream.");
  }
}
// Stream callback triggers instantly when changes happen on the dashboard
void streamCallback(FirebaseStream data) {
  Serial.print("Configuration stream event received. Path: ");
  Serial.println(data.dataPath());
  
  if (data.dataPath() == "/") {
    FirebaseJson &json = data.jsonObject();
    FirebaseJsonData jsonData;
    
    if (json.get(jsonData, "isEnabled")) {
      isEnabled = jsonData.boolValue;
    }
    if (json.get(jsonData, "threshold")) {
      soundThreshold = jsonData.intValue;
    }
    if (json.get(jsonData, "wifiControl")) {
      String controlCmd = jsonData.stringValue;
      if (controlCmd == "disconnect") {
        wifiDisconnect = true;
      } else if (controlCmd == "forget") {
        wifiForget = true;
      }
    }
  } 
  else if (data.dataPath() == "/isEnabled") {
    isEnabled = data.boolData();
  } 
  else if (data.dataPath() == "/threshold") {
    soundThreshold = data.intData();
  }
  else if (data.dataPath() == "/wifiControl") {
    String controlCmd = data.stringData();
    if (controlCmd == "disconnect") {
      wifiDisconnect = true;
    } else if (controlCmd == "forget") {
      wifiForget = true;
    }
  }
  
  Serial.print("Updated States -> Enabled: ");
  Serial.print(isEnabled ? "YES" : "NO");
  Serial.print(" | Threshold: ");
  Serial.print(soundThreshold);
  Serial.println(" dB");
}
void streamTimeoutCallback(bool timeout) {
  if (timeout) {
    Serial.println("Stream timeout... re-establishing.");
  }
}
// Read microphone sensor and report violations
void monitorSound() {
  int rawValue = analogRead(MIC_ANALOG_PIN);
  int dbLevel = map(rawValue, 0, 4095, 30, 120); 
  
  if (dbLevel > soundThreshold) {
    Serial.print("THRESHOLD EXCEEDED! Current level: ");
    Serial.print(dbLevel);
    Serial.print(" dB | Limit: ");
    Serial.print(soundThreshold);
    Serial.println(" dB");
    // Write violation to user's dashboard
    String path = "/users/" + ownerUid + "/zones/" + zoneId + "/devices/" + deviceMac + "/violations";
    String vId = "v-" + String(millis()); // Generate a unique violation ID
    
    FirebaseJson violation;
    violation.add("id", vId);
    violation.add("timestamp", "2026-06-20T00:00:00Z"); // replace with real NTP timestamp in production
    violation.add("duration", "4s");
    violation.add("threshold", soundThreshold);
    
    if (Firebase.RTDB.setJSON(&fbdoWrite, (path + "/" + vId).c_str(), &violation)) {
      Serial.println("Violation successfully reported to Firebase.");
    } else {
      Serial.print("Failed to report violation. Reason: ");
      Serial.println(fbdoWrite.errorReason());
    }
    
    delay(5000); 
  }
}