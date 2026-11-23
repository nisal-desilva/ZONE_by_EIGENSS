/**
 * Z.O.N.E. Fully Integrated Device Firmware
 * 
 * Combines:
 * - 3-Microphone Decibel/RMS monitoring & WS2812B FastLED display/ring
 * - Local physical buttons & standby power control
 * - Under-threshold alarms playing voice messages via I2S Speaker
 * - Non-blocking background Wi-Fi connection & BLE provisioning
 * - Real-time bidirectional database synchronization & status heartbeats
 * - 5 plays in 4 mins -> database violation reports
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
#include <driver/i2s.h>
#include <math.h>
#define FASTLED_RMT5_RECYCLE 1
#include <FastLED.h>
#include "audio_data.h" // Local voice message raw data array

// Provide the token generation and SDK helper processes
#include <addons/TokenHelper.h>
#include <addons/RTDBHelper.h>

// --- Firebase Configuration ---
#define FIREBASE_API_KEY "AIzaSyBSDsX8rkYPWMBzlHU2tioMeiJ28W4MESs"
#define FIREBASE_DATABASE_URL "https://zone-c0177-default-rtdb.asia-southeast1.firebasedatabase.app"

// --- BLE UUIDs ---
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

// ==========================================
// SYSTEM STATE & PINS
// ==========================================
bool systemActive = true; 
bool lastSystemState = true; 

#define BTN_MINUS 5
#define BTN_PLUS  7
#define BTN_POWER 15

unsigned long lastDebounceTime = 0;  
const unsigned long debounceDelay = 200; 

// --- UI OVERLAY & DISPLAY VARIABLES ---
unsigned long thresholdDisplayTimer = 0;
bool showThresholdOverlay = false;
const unsigned long OVERLAY_DURATION = 2000; 

unsigned long lastVisualUpdate = 0; 
float peakPercentage = 0.0f;        

// ==========================================
// FASTLED DISPLAY & RING SETTINGS
// ==========================================
#define DISP_PIN 37        
#define NUM_DISP_LEDS 48
#define RING_PIN 36       
#define NUM_RING_LEDS 48
#define BRIGHTNESS 100 

CRGB dispLeds[NUM_DISP_LEDS];
CRGB ringLeds[NUM_RING_LEDS];

const byte digitPatterns[10][7] = {
  {1, 1, 1, 1, 1, 1, 0}, {0, 1, 1, 0, 0, 0, 0}, {1, 1, 0, 1, 1, 0, 1}, 
  {1, 1, 1, 1, 0, 0, 1}, {0, 1, 1, 0, 0, 1, 1}, {1, 0, 1, 1, 0, 1, 1}, 
  {1, 0, 1, 1, 1, 1, 1}, {1, 1, 1, 0, 0, 0, 0}, {1, 1, 1, 1, 1, 1, 1}, 
  {1, 1, 1, 1, 0, 1, 1}
};

const int d1_B[3] = {0, 1, 2}; const int d1_C[3] = {3, 4, 5};
const int d2[7][3] = {{11, 10, 9}, {8, 7, 6}, {18, 19, 20}, {23, 22, 21}, {26, 25, 24}, {12, 13, 14}, {15, 16, 17}};
const int d3[7][3] = {{30, 31, 32}, {33, 34, 35}, {47, 46, 45}, {42, 43, 44}, {39, 40, 41}, {29, 28, 27}, {38, 37, 36}};

// ==========================================
// I2S AUDIO SETTINGS & PINS
// ==========================================
#define I2S0_MIC_SCK  40
#define I2S0_MIC_WS   41
#define I2S0_MIC_SD   39

#define I2S1_SHARED_SCK  17
#define I2S1_SHARED_WS   18
#define I2S1_MIC_SD      8 
#define I2S_SPK_DOUT     16

float calibration_offset = -1.0f; // Calibrated to trigger precisely at the target 65dB library threshold range
float user_threshold = 65.0f; 

unsigned long hitTimes[5] = {0, 0, 0, 0, 0}; 
int hitCount = 0;                            

// ==========================================
// FIREBASE CLOUD & TELEMETRY STATES
// ==========================================
String ssid = "";
String pass = "";
String deviceMac = "";
String ownerUid = "";
String zoneId = "";
bool isPaired = false;
bool wifiDisconnect = false;
bool wifiForget = false;
bool dbSyncNeeded = false; // Flag to push threshold/active status to Firebase when changed locally
bool wifiWasConnected = false;
bool wifiWasDisconnected = false;
bool bleActive = false;    // Tracks if device is in BLE provisioning mode
volatile bool bleRequestRestart = false;
volatile bool bleRequestForget = false;
volatile bool bleRequestConnectSaved = false;
String bleNewSSID = "";
String bleNewPass = "";

// Rolling array for recording last 5 playback timestamps to track violations (5 plays in 4 mins)
unsigned long playTimes[5] = {0, 0, 0, 0, 0};
int playCount = 0;

// File paths in LittleFS for credentials
const char* ssidPath = "/ssid.txt";
const char* passPath = "/pass.txt";
const char* thresholdPath = "/threshold.txt";
const char* standbyPath = "/standby.txt";

// BLE provisioning configurations
bool deviceConnected = false;
BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;

// Firebase Data Objects
FirebaseData fbdoHandshake;
FirebaseData fbdoStream;
FirebaseData fbdoWrite;
FirebaseAuth auth;
FirebaseConfig config;

// Function Prototypes
String getFormattedMacAddress();
void performDatabaseHandshake();
void startRealtimeStream();
void streamCallback(FirebaseStream data);
void streamTimeoutCallback(bool timeout);
void checkFirebaseStatus();
void reportViolation();
void initLittleFS();
String readFile(const char * path);
void writeFile(const char * path, const char * message);
bool connectWiFi();
void startBLEProvisioning();
void customLoopTask(void *pvParameters);
void customLoop();

// ==========================================
// AUDIO INIT FUNCTIONS
// ==========================================
void initMicPair() {
  i2s_config_t cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = 16000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 256,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };
  i2s_pin_config_t pins = {
    .bck_io_num = I2S0_MIC_SCK,
    .ws_io_num = I2S0_MIC_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S0_MIC_SD
  };
  i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pins);
}

void initMic3() {
  i2s_config_t cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = 16000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 256,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };
  i2s_pin_config_t pins = {
    .bck_io_num = I2S1_SHARED_SCK,
    .ws_io_num = I2S1_SHARED_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S1_MIC_SD
  };
  i2s_driver_install(I2S_NUM_1, &cfg, 0, NULL);
  i2s_set_pin(I2S_NUM_1, &pins);
}

void initSpeaker() {
  i2s_config_t spk_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = 44100, 
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 64,
    .use_apll = false,
    .tx_desc_auto_clear = true
  };
  i2s_pin_config_t spk_pins = {
    .bck_io_num = I2S1_SHARED_SCK,
    .ws_io_num = I2S1_SHARED_WS,
    .data_out_num = I2S_SPK_DOUT,
    .data_in_num = I2S_PIN_NO_CHANGE
  };
  i2s_driver_install(I2S_NUM_1, &spk_config, 0, NULL);
  i2s_set_pin(I2S_NUM_1, &spk_pins);
}

void playVoiceMessage() {
  size_t bytes_written;
  int volume_multiplier = 4; 
  int16_t* audio_samples = (int16_t*)(audiomass_output + 44);
  size_t sample_count = (sizeof(audiomass_output) - 44) / 2;
  
  for (size_t i = 0; i < sample_count; i++) {
    int32_t amplified_sample = audio_samples[i] * volume_multiplier;
    if (amplified_sample > 32767) amplified_sample = 32767;
    if (amplified_sample < -32768) amplified_sample = -32768;
    int16_t final_sample = (int16_t)amplified_sample;
    i2s_write(I2S_NUM_1, &final_sample, sizeof(int16_t), &bytes_written, portMAX_DELAY);
  }
}

float rmsToDb(float mean_square) {
  if (mean_square <= 0.0f) return -999.0f;
  return 10.0f * log10(mean_square) + calibration_offset;
}

// ==========================================
// FASTLED DRAWING FUNCTIONS
// ==========================================
void drawNumber(int num, CRGB color) {
  if (num > 199) num = 199; if (num < 0) num = 0; 
  int hundreds = num / 100; int tens = (num / 10) % 10; int units = num % 10;

  if (hundreds == 1) {
    for(int i = 0; i < 3; i++) dispLeds[d1_B[i]] = color;
    for(int i = 0; i < 3; i++) dispLeds[d1_C[i]] = color;
  }
  if (num >= 10 || hundreds == 1) { 
    for (int seg = 0; seg < 7; seg++) {
      if (digitPatterns[tens][seg] == 1) {
        for (int i = 0; i < 3; i++) dispLeds[d2[seg][i]] = color;
      }
    }
  }
  for (int seg = 0; seg < 7; seg++) {
    if (digitPatterns[units][seg] == 1) {
      for (int i = 0; i < 3; i++) dispLeds[d3[seg][i]] = color;
    }
  }
}

void drawRing(int num, CRGB color) {
  if (num > 199) num = 199; if (num < 0) num = 0;
  int ledsToLight = map(num, 0, 100, 0, NUM_RING_LEDS);
  if (num > 0 && ledsToLight == 0) ledsToLight = 1;
  if (ledsToLight > NUM_RING_LEDS) ledsToLight = NUM_RING_LEDS; 
  for (int i = 0; i < ledsToLight; i++) ringLeds[i] = color; 
}

// ==========================================
// BUTTON, OVERLAY, AND POWER HANDLER
// ==========================================
void handleButtonsAndOverlay(bool isAlarm) {
  
  // 1. Check Power Button First
  if (digitalRead(BTN_POWER) == LOW) {
    if ((millis() - lastDebounceTime) > debounceDelay) {
      systemActive = !systemActive;
      dbSyncNeeded = true; // Request sync to cloud
      writeFile(standbyPath, systemActive ? "1" : "0");
      lastDebounceTime = millis();
    }
  }

  // If system is in standby, completely block the rest of the UI
  if (!systemActive) return;

  // 2. Process Plus/Minus Buttons
  if (digitalRead(BTN_PLUS) == LOW) {
    if ((millis() - lastDebounceTime) > debounceDelay) {
      user_threshold += 1.0f;
      if (user_threshold > 120.0f) user_threshold = 120.0f; 
      showThresholdOverlay = true;
      dbSyncNeeded = true; // Request sync to cloud
      writeFile(thresholdPath, String(user_threshold).c_str());
      thresholdDisplayTimer = millis();
      lastDebounceTime = millis();
    }
  }
  if (digitalRead(BTN_MINUS) == LOW) {
    if ((millis() - lastDebounceTime) > debounceDelay) {
      user_threshold -= 1.0f;
      if (user_threshold < 40.0f) user_threshold = 40.0f; 
      showThresholdOverlay = true;
      dbSyncNeeded = true; // Request sync to cloud
      writeFile(thresholdPath, String(user_threshold).c_str());
      thresholdDisplayTimer = millis();
      lastDebounceTime = millis();
    }
  }

  // 3. Render Blue Overlay OR Enforce Alarm Red
  if (showThresholdOverlay) {
    if (millis() - thresholdDisplayTimer < OVERLAY_DURATION) {
      FastLED.clear(); 
      drawNumber((int)user_threshold, CRGB::Blue); 
      drawRing((int)user_threshold, CRGB::Blue);
      FastLED.show(); 
    } else {
      showThresholdOverlay = false; 
    }
  } 
  
  // If overlay is off, and we are currently frozen in an alarm, enforce RED 100
  if (!showThresholdOverlay && isAlarm) {
      FastLED.clear(); 
      drawNumber(100, CRGB::Red); 
      drawRing(100, CRGB::Red);
      FastLED.show(); 
  }
}

// ==========================================
// ACTIVE DELAY FUNCTION
// ==========================================
void activeDelay(unsigned long ms) {
  unsigned long start = millis();
  while (millis() - start < ms) {
    handleButtonsAndOverlay(true); // 'true' tells the UI to stay RED
    checkFirebaseStatus(); // Keep the cloud stream alive during alarms!
    delay(10); 
  }
}

// ==========================================
// LITTLEFS & STORAGE HANDLERS
// ==========================================
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

// ==========================================
// BLE CALLBACKS
// ==========================================
class MyBLECallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pChar) {
      String rxValue = String(pChar->getValue().c_str());
      if (rxValue.length() > 0) {
        String data = rxValue;
        data.trim();
        Serial.print("Received BLE Data: ");
        Serial.println(data);

        if (data == "CONNECT_SAVED") {
          bleRequestConnectSaved = true;
          bleRequestRestart = true;
        } 
        else if (data == "FORGET") {
          bleRequestForget = true;
          bleRequestRestart = true;
        } 
        else {
          // Expecting data in format: "SSID,PASSWORD"
          int commaIndex = data.indexOf(',');
          if (commaIndex > 0) {
            bleNewSSID = data.substring(0, commaIndex);
            bleNewPass = data.substring(commaIndex + 1);
            bleNewSSID.trim();
            bleNewPass.trim();
            bleRequestRestart = true;
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
  
  Serial.println("BLE Advertising started successfully.");
}

// ==========================================
// CLOUD SYSTEM HANDSHAKE & STREAMING
// ==========================================
String getFormattedMacAddress() {
  uint8_t mac[6];
  if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
    char macStr[13];
    snprintf(macStr, sizeof(macStr), "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(macStr);
  }
  String rawMac = WiFi.macAddress();
  rawMac.replace(":", "");
  rawMac.toUpperCase();
  return rawMac;
}

void performDatabaseHandshake() {
  Serial.println("Performing registry handshake...");
  String path = "/devices/" + deviceMac;
  
  if (Firebase.RTDB.getJSON(&fbdoHandshake, path.c_str())) {
    FirebaseJson &json = fbdoHandshake.jsonObject();
    FirebaseJsonData jsonData;
    
    json.get(jsonData, "paired");
    if (jsonData.success && jsonData.typeNum == FirebaseJson::JSON_BOOL && jsonData.boolValue == true) {
      
      json.get(jsonData, "ownerUid");
      if (jsonData.success && jsonData.typeNum == FirebaseJson::JSON_STRING) {
        ownerUid = jsonData.stringValue;
      }
      
      json.get(jsonData, "zoneId");
      if (jsonData.success && jsonData.typeNum == FirebaseJson::JSON_STRING) {
        zoneId = jsonData.stringValue;
        isPaired = true;
        
        Serial.println("====================================");
        Serial.println("Pairing Status: REGISTERED & PAIRED!");
        Serial.print("Owner UID: "); Serial.println(ownerUid);
        Serial.print("Zone ID: "); Serial.println(zoneId);
        Serial.println("====================================");
        
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

void startRealtimeStream() {
  String devicePath = "/users/" + ownerUid + "/zones/" + zoneId + "/devices/" + deviceMac;
  
  // Write current physical configurations to Firebase on connection to synchronize dashboard
  Firebase.RTDB.setBool(&fbdoWrite, (devicePath + "/isOnline").c_str(), true);
  Firebase.RTDB.setString(&fbdoWrite, (devicePath + "/wifiControl").c_str(), "");
  Firebase.RTDB.setBool(&fbdoWrite, (devicePath + "/isEnabled").c_str(), systemActive);
  Firebase.RTDB.setInt(&fbdoWrite, (devicePath + "/threshold").c_str(), (int)user_threshold);
  Firebase.RTDB.setString(&fbdoWrite, (devicePath + "/savedSsid").c_str(), ssid);
  
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

void streamCallback(FirebaseStream data) {
  Serial.print("Configuration stream event received. Path: ");
  Serial.println(data.dataPath());
  
  if (data.dataPath() == "/") {
    FirebaseJson &json = data.jsonObject();
    FirebaseJsonData jsonData;
    
    if (json.get(jsonData, "isEnabled")) {
      bool targetState = jsonData.boolValue;
      if (systemActive != targetState) {
        systemActive = targetState;
        writeFile(standbyPath, systemActive ? "1" : "0");
        Serial.print("Stream -> standby set to: ");
        Serial.println(systemActive ? "ACTIVE" : "STANDBY");
      }
    }
    if (json.get(jsonData, "threshold")) {
      float targetThreshold = (float)jsonData.intValue;
      if (user_threshold != targetThreshold) {
        user_threshold = targetThreshold;
        writeFile(thresholdPath, String(user_threshold).c_str());
        Serial.print("Stream -> threshold set to: ");
        Serial.print(user_threshold);
        Serial.println(" dB");
        
        // Show threshold overlay on display
        showThresholdOverlay = true;
        thresholdDisplayTimer = millis();
      }
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
    bool targetState = data.boolData();
    if (systemActive != targetState) {
      systemActive = targetState;
      writeFile(standbyPath, systemActive ? "1" : "0");
      Serial.print("Stream -> standby set to: ");
      Serial.println(systemActive ? "ACTIVE" : "STANDBY");
    }
  } 
  else if (data.dataPath() == "/threshold") {
    float targetThreshold = (float)data.intData();
    if (user_threshold != targetThreshold) {
      user_threshold = targetThreshold;
      writeFile(thresholdPath, String(user_threshold).c_str());
      Serial.print("Stream -> threshold set to: ");
      Serial.print(user_threshold);
      Serial.println(" dB");
      
      // Show threshold overlay on display
      showThresholdOverlay = true;
      thresholdDisplayTimer = millis();
    }
  }
  else if (data.dataPath() == "/wifiControl") {
    String controlCmd = data.stringData();
    if (controlCmd == "disconnect") {
      wifiDisconnect = true;
    } else if (controlCmd == "forget") {
      wifiForget = true;
    }
  }
}

void streamTimeoutCallback(bool timeout) {
  if (timeout) {
    Serial.println("Stream timeout... re-establishing.");
  }
}

// ==========================================
// ASYNCHRONOUS WIFI & FIREBASE MONITOR
// ==========================================
void checkFirebaseStatus() {
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck < 2000) return; // run status checks every 2 seconds
  lastCheck = millis();

  if (WiFi.status() == WL_CONNECTED) {
    if (!wifiWasConnected) {
      Serial.println("\nWi-Fi connected successfully in background!");
      Serial.print("IP Address: ");
      Serial.println(WiFi.localIP());
      wifiWasConnected = true;
      wifiWasDisconnected = false;
      
      // Initialize Firebase (Only once)
      static bool firebaseInitialized = false;
      if (!firebaseInitialized) {
        config.api_key = FIREBASE_API_KEY;
        config.database_url = FIREBASE_DATABASE_URL;
        config.token_status_callback = tokenStatusCallback;
        
        // Clean auth structure to trigger anonymous login automatically
        auth.user.email = "";
        auth.user.password = "";
        
        fbdoHandshake.setBSSLBufferSize(4096, 1024);
        fbdoHandshake.setResponseSize(4096);
        fbdoStream.setBSSLBufferSize(8192, 1024);
        fbdoStream.setResponseSize(8192);
        fbdoWrite.setBSSLBufferSize(4096, 1024);
        fbdoWrite.setResponseSize(4096);
        
        config.timeout.socketConnection = 30 * 1000;
        config.timeout.serverResponse = 10 * 1000;
        config.timeout.rtdbKeepAlive = 45 * 1000;
        config.timeout.rtdbStreamReconnect = 1000;
        
        Firebase.reconnectWiFi(true);
        
        // Trigger anonymous sign-in
        if (Firebase.signUp(&config, &auth, "", "")) {
          Serial.println("Anonymous sign-in successfully initialized.");
        } else {
          Serial.println("Anonymous sign-in initialization failed.");
        }

        Firebase.begin(&config, &auth); 
        firebaseInitialized = true;
        Serial.println("Firebase Client initialized.");
      }
    }
    
    if (Firebase.ready()) {
      if (!isPaired) {
        performDatabaseHandshake();
      } else {
        // Stream watchdog
        if (!fbdoStream.httpConnected()) {
          Serial.println("Stream connection lost! Re-establishing stream...");
          Firebase.RTDB.endStream(&fbdoStream);
          delay(100);
          startRealtimeStream();
        }
        
        // Heartbeat: Write server timestamp to lastSeen every 5 seconds
        static unsigned long lastHeartbeat = 0;
        if (millis() - lastHeartbeat > 5000) {
          lastHeartbeat = millis();
          String devicePath = "/users/" + ownerUid + "/zones/" + zoneId + "/devices/" + deviceMac;
          Firebase.RTDB.setTimestamp(&fbdoWrite, (devicePath + "/lastSeen").c_str());
        }
      }
    }
  } else {
    if (!wifiWasDisconnected) {
      Serial.println("Wi-Fi offline. Operating in local mode.");
      wifiWasDisconnected = true;
      wifiWasConnected = false;
      isPaired = false;
    }
  }
}

// ==========================================
// CLOUD VIOLATION REPORTING
// ==========================================
void reportViolation() {
  if (WiFi.status() == WL_CONNECTED && Firebase.ready() && isPaired) {
    Serial.println(">>>> REPORTING VIOLATION TO CLOUD <<<<");
    String path = "/users/" + ownerUid + "/zones/" + zoneId + "/devices/" + deviceMac + "/violations";
    String vId = "v-" + String(millis());
    
    FirebaseJson violation;
    violation.add("id", vId);
    violation.set("timestamp/.sv", "timestamp"); // Firebase RTDB server timestamp placeholder
    violation.add("duration", "4s");
    violation.add("threshold", (int)user_threshold);
    
    if (Firebase.RTDB.setJSON(&fbdoWrite, (path + "/" + vId).c_str(), &violation)) {
      Serial.println("Violation successfully reported to Firebase.");
    } else {
      Serial.print("Failed to report violation. Reason: ");
      Serial.println(fbdoWrite.errorReason());
    }
  } else {
    Serial.println("Violation recorded, but device is offline. Skipping database sync.");
  }
}

// --- Premium LED boot & connection indicators ---
void showWiFiConnectingPattern() {
  static int dotPos = 0;
  FastLED.clear();
  for (int i = 0; i < 5; i++) {
    int pos = (dotPos - i + NUM_RING_LEDS) % NUM_RING_LEDS;
    int val = 255 - (i * 50);
    if (val < 0) val = 0;
    ringLeds[pos] = CRGB(0, 0, val); // Blue trailing spinner
  }
  FastLED.show();
  dotPos = (dotPos + 1) % NUM_RING_LEDS;
  delay(30);
}

void showSuccessPattern() {
  FastLED.clear();
  for (int i = 0; i < NUM_RING_LEDS; i++) {
    ringLeds[i] = CRGB(0, 255, 0); // Solid Green
  }
  FastLED.show();
  delay(1500);
  FastLED.clear();
  FastLED.show();
}

void showFailPattern() {
  FastLED.clear();
  for (int i = 0; i < NUM_RING_LEDS; i++) {
    ringLeds[i] = CRGB(255, 0, 0); // Solid Red
  }
  FastLED.show();
  delay(1000);
  FastLED.clear();
  FastLED.show();
}

void showBLEPattern() {
  static bool ledsSet = false;
  if (ledsSet) return;
  ledsSet = true;

  FastLED.clear();
  for (int i = 0; i < NUM_RING_LEDS; i++) {
    ringLeds[i] = CRGB(0, 0, 80); // Pleasant steady blue indicator for BLE mode
  }
  FastLED.show();
}

bool connectWiFi() {
  if (ssid == "") return false;

  Serial.print("Connecting to Wi-Fi: ");
  Serial.println(ssid);
  
  WiFi.mode(WIFI_STA);
  WiFi.setTxPower(WIFI_POWER_8_5dBm); // Reduce Wi-Fi transmission power to prevent LDO regulator brownouts
  WiFi.begin(ssid.c_str(), pass.c_str());

  unsigned long startAttemptTime = millis();
  const unsigned long connectionTimeout = 10000; // 10 seconds

  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < connectionTimeout) {
    showWiFiConnectingPattern();
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nSuccessfully connected to Wi-Fi!");
    showSuccessPattern();
    return true;
  } else {
    Serial.println("\nConnection timed out.");
    showFailPattern();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    return false;
  }
}

// ==========================================
// MAIN SETUP
// ==========================================
void setup() {
  Serial.begin(115200);
  // Wait up to 3 seconds for Serial connection (so it doesn't freeze in standalone mode)
  while (!Serial && millis() < 3000) {
    delay(10);
  }
  Serial.println("\n--- Z.O.N.E. Custom Hardware Booting ---");

  pinMode(BTN_MINUS, INPUT_PULLUP);
  pinMode(BTN_PLUS, INPUT_PULLUP);
  pinMode(BTN_POWER, INPUT_PULLUP);
  pinMode(6, INPUT); // Safe high-impedance mode to prevent shorting issues on this pin

  // Initialize display LED arrays
  FastLED.addLeds<WS2812B, DISP_PIN, GRB>(dispLeds, NUM_DISP_LEDS);
  FastLED.addLeds<WS2812B, RING_PIN, GRB>(ringLeds, NUM_RING_LEDS);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, 2000); 
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.clear();
  FastLED.show();
  
  // Set up microphone components
  initMicPair();
  initMic3();
  pinMode(I2S_SPK_DOUT, OUTPUT);
  digitalWrite(I2S_SPK_DOUT, LOW);

  delay(1500); 
  size_t dummyBytes;
  int32_t dummyBuf[256];
  for(int i = 0; i < 5; i++) {
    i2s_read(I2S_NUM_0, dummyBuf, sizeof(dummyBuf), &dummyBytes, 100 / portTICK_PERIOD_MS);
    i2s_read(I2S_NUM_1, dummyBuf, sizeof(dummyBuf), &dummyBytes, 100 / portTICK_PERIOD_MS);
  }

  // Retrieve MAC address
  deviceMac = getFormattedMacAddress();
  Serial.print("Device MAC Address: ");
  Serial.println(deviceMac);

  initLittleFS();
  ssid = readFile(ssidPath);
  pass = readFile(passPath);

  // Retrieve saved configurations from LittleFS if present
  String savedThreshold = readFile(thresholdPath);
  if (savedThreshold != "") {
    user_threshold = savedThreshold.toFloat();
    Serial.printf("Restored user_threshold from LittleFS: %.1f dB\n", user_threshold);
  }
  String savedStandby = readFile(standbyPath);
  if (savedStandby != "") {
    systemActive = (savedStandby == "1");
    Serial.printf("Restored systemActive from LittleFS: %s\n", systemActive ? "ACTIVE" : "STANDBY");
  }

  // Try to connect to Wi-Fi. If we fail or have no credentials, launch BLE provisioning
  if (ssid != "" && connectWiFi()) {
    bleActive = false;
    Serial.println("Wi-Fi connected. BLE provisioning is disabled.");
  } else {
    if (ssid == "") {
      // First-time setup: No credentials. Block main loop, wait for BLE pairing (show blue ring)
      bleActive = true;
      Serial.println("No Wi-Fi credentials. Launching BLE provisioning in configuration mode...");
    } else {
      // Running locally: Has credentials but failed to connect (offline). Run microphone loop normally, but advertise BLE in background
      bleActive = false;
      Serial.println("Failed to connect to Wi-Fi. Running locally and launching BLE in background...");
    }
    startBLEProvisioning();
  }

  // Create custom loop task with 16KB stack on Core 1 (same as default loopTask priority/core)
  xTaskCreatePinnedToCore(
    customLoopTask,
    "CustomLoopTask",
    16384,
    NULL,
    1,
    NULL,
    1
  );

  Serial.println("Custom loop task started. Deleting default loopTask.");
  vTaskDelete(NULL);
}

// ==========================================
// MAIN LOOP
// ==========================================
void customLoopTask(void *pvParameters) {
  for (;;) {
    customLoop();
  }
}

void loop() {
  // Empty loop since default loopTask is deleted
}

void customLoop() {
  // Check for BLE restart requests (e.g., reconnect or new credentials sent over Bluetooth)
  if (bleRequestRestart) {
    // Small delay to allow the BLE client to safely receive the GATT write response packet
    delay(500);
    
    if (bleRequestForget) {
      Serial.println("BLE requested: FORGET. Clearing credentials...");
      writeFile(ssidPath, "");
      writeFile(passPath, "");
    } 
    else if (bleRequestConnectSaved) {
      Serial.println("BLE requested: CONNECT_SAVED.");
    } 
    else if (bleNewSSID != "") {
      Serial.println("BLE requested: Credentials Save.");
      writeFile(ssidPath, bleNewSSID.c_str());
      writeFile(passPath, bleNewPass.c_str());
      
      // Show success visual
      FastLED.clear();
      for (int i = 0; i < NUM_RING_LEDS; i++) {
        ringLeds[i] = CRGB(0, 0, 255); // Solid Blue
      }
      FastLED.show();
    }
    
    Serial.println("Restarting ESP32 in 2 seconds...");
    delay(2000);
    ESP.restart();
  }

  if (bleActive) {
    showBLEPattern();
    handleButtonsAndOverlay(false);
    delay(10);
    return;
  }
  
  // ---------- 1. Handle background connectivity & sync ----------
  checkFirebaseStatus();

  // Debug printing when local change needs to sync
  if (dbSyncNeeded) {
    Serial.printf("[DEBUG] dbSyncNeeded: TRUE. WiFi: %d, FirebaseReady: %d, isPaired: %d\n",
                  (WiFi.status() == WL_CONNECTED), Firebase.ready(), isPaired);
  }

  // If local button presses changed user settings, push them to Firebase
  if (dbSyncNeeded && WiFi.status() == WL_CONNECTED && Firebase.ready() && isPaired) {
    String devicePath = "/users/" + ownerUid + "/zones/" + zoneId + "/devices/" + deviceMac;
    
    // Use leaf-level writes (identical to startRealtimeStream) which are extremely robust
    bool ok1 = Firebase.RTDB.setBool(&fbdoWrite, (devicePath + "/isEnabled").c_str(), systemActive);
    bool ok2 = Firebase.RTDB.setInt(&fbdoWrite, (devicePath + "/threshold").c_str(), (int)user_threshold);
    
    if (ok1 && ok2) {
      dbSyncNeeded = false;
      Serial.println("Local state sync pushed to Firebase successfully.");
    } else {
      Serial.printf("Local state sync failed. isEnabled write: %d, threshold write: %d\n", ok1, ok2);
      Serial.print("Reason: ");
      Serial.println(fbdoWrite.errorReason());
    }
  }

  // Handle WiFi Disconnection stream command
  if (wifiDisconnect) {
    Serial.println("wifiControl: disconnect command received.");
    String devicePath = "/users/" + ownerUid + "/zones/" + zoneId + "/devices/" + deviceMac;
    Firebase.RTDB.setString(&fbdoWrite, (devicePath + "/wifiControl").c_str(), "");
    delay(200);
    Firebase.RTDB.setBool(&fbdoWrite, (devicePath + "/isOnline").c_str(), false);
    
    delay(500);
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    wifiDisconnect = false;
    isPaired = false;
    
    // Start BLE advertising in background so the user can reconnect the device later
    Serial.println("Starting BLE advertising in background...");
    startBLEProvisioning();
    
    Serial.println("Wi-Fi disconnected. Operating in local mode.");
  }

  // Handle WiFi Forget credentials stream command
  if (wifiForget) {
    Serial.println("wifiControl: forget command received.");
    String devicePath = "/users/" + ownerUid + "/zones/" + zoneId + "/devices/" + deviceMac;
    Firebase.RTDB.setString(&fbdoWrite, (devicePath + "/wifiControl").c_str(), "");
    delay(200);
    Firebase.RTDB.setBool(&fbdoWrite, (devicePath + "/isOnline").c_str(), false);
    
    delay(500);
    writeFile(ssidPath, "");
    writeFile(passPath, "");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    wifiForget = false;
    isPaired = false;
    
    Serial.println("Credentials forgotten. Restarting...");
    delay(1000);
    ESP.restart();
  }

  // ---------- 2. Handle UI & Power Logic ----------
  handleButtonsAndOverlay(false);

  // ---------- 3. Check Standby Transitions ----------
  if (systemActive != lastSystemState) {
    if (!systemActive) {
      // Going to sleep: Turn off screen immediately
      FastLED.clear();
      FastLED.show();
      Serial.println(">>> System Standby Mode: ON <<<");
    } else {
      Serial.println(">>> System Standby Mode: OFF <<<");
      // Trigger showing the current threshold overlay when waking up
      showThresholdOverlay = true;
      thresholdDisplayTimer = millis();
    }
    lastSystemState = systemActive;
  }

  // If system is standby, save CPU and skip microphone processing
  if (!systemActive) {
    delay(50);
    return;
  }

  // ---------- 4. Read Microphones ----------
  int32_t pairBuf[128];
  size_t bytesRead0 = 0;
  i2s_read(I2S_NUM_0, pairBuf, sizeof(pairBuf), &bytesRead0, 100 / portTICK_PERIOD_MS);
  int sampleCount0 = bytesRead0 / sizeof(int32_t);
  int frameCount = sampleCount0 / 2;

  float msMic1 = 0.0f; float msMic2 = 0.0f;
  for (int i = 0; i < frameCount; i++) {
    int32_t leftSample  = pairBuf[2 * i]     >> 14;
    int32_t rightSample = pairBuf[2 * i + 1] >> 14;
    msMic1 += (float)leftSample * leftSample;
    msMic2 += (float)rightSample * rightSample;
  }
  if (frameCount > 0) { msMic1 /= frameCount; msMic2 /= frameCount; }

  int32_t mic3Buf[64];
  size_t bytesRead1 = 0;
  i2s_read(I2S_NUM_1, mic3Buf, sizeof(mic3Buf), &bytesRead1, 100 / portTICK_PERIOD_MS);
  int sampleCount1 = bytesRead1 / sizeof(int32_t);
  float msMic3 = 0.0f;
  for (int i = 0; i < sampleCount1; i++) {
    int32_t s = mic3Buf[i] >> 14;
    msMic3 += (float)s * s;
  }
  if (sampleCount1 > 0) { msMic3 /= sampleCount1; }

  // ---------- 5. Calculate Noise Math ----------
  bool error_found = false;
  if (msMic1 <= 0.0f || msMic2 <= 0.0f || msMic3 <= 0.0f) error_found = true;

  if (!error_found) {
    float dB1 = rmsToDb(msMic1);
    float dB2 = rmsToDb(msMic2);
    float dB3 = rmsToDb(msMic3);
    float average_dB = (dB1 + dB2 + dB3) / 3.0f;

    float percentage = (average_dB / user_threshold) * 100.0f;
    if (percentage > peakPercentage) peakPercentage = percentage; 

    int alarmVal = (int)percentage;
    if (alarmVal < 0) alarmVal = 0;
    if (alarmVal > 100) alarmVal = 100;

    // ---------- 6. Normal Visual Update (2Hz Refresh) ----------
    if (millis() - lastVisualUpdate >= 500) {
      lastVisualUpdate = millis();
      
      int displayVal = (int)peakPercentage;
      if (displayVal > 100) displayVal = 100;
      peakPercentage = 0.0f; 

      if (!showThresholdOverlay) {
        CRGB thresholdColor;
        if (displayVal <= 50) thresholdColor = CRGB::Green;
        else if (displayVal < 80) thresholdColor = CRGB::Yellow;
        else if (displayVal < 100) thresholdColor = CRGB::OrangeRed;
        else thresholdColor = CRGB::Red; 
        
        FastLED.clear(); 
        drawNumber(displayVal, thresholdColor); 
        drawRing(displayVal, thresholdColor);   
        FastLED.show(); 
      }
    }

    // ---------- 7. ALARM TRIGGER LOGIC ----------
    if (!showThresholdOverlay && alarmVal == 100) {
      
      for (int i = 0; i < 4; i++) {
        hitTimes[i] = hitTimes[i+1];
      }
      hitTimes[4] = millis();
      if (hitCount < 5) hitCount++;

      if (hitCount == 5 && (hitTimes[4] - hitTimes[0] <= 20000)) {
        
        Serial.println(">>>> ALARM TRIGGERED! <<<<");

        // Force visual display override to RED 100 immediately
        FastLED.clear(); 
        drawNumber(100, CRGB::Red); 
        drawRing(100, CRGB::Red);
        FastLED.show();

        // 1. Teardown Mics
        i2s_stop(I2S_NUM_0); 
        i2s_stop(I2S_NUM_1);
        delay(10); 
        i2s_driver_uninstall(I2S_NUM_1);
        delay(10);

        // 2. Play Audio
        initSpeaker();
        delay(50); 
        playVoiceMessage();
        delay(200);

        // 3. Teardown Speaker
        i2s_stop(I2S_NUM_1);
        delay(10);
        i2s_driver_uninstall(I2S_NUM_1);
        delay(10);
        pinMode(I2S_SPK_DOUT, OUTPUT);
        digitalWrite(I2S_SPK_DOUT, LOW);

        // Record audio playback timestamp in the rolling array
        for (int i = 0; i < 4; i++) {
          playTimes[i] = playTimes[i+1];
        }
        playTimes[4] = millis();
        if (playCount < 5) playCount++;

        // 4. Check for Violation (5 playbacks within 4 minutes)
        if (playCount == 5 && (playTimes[4] - playTimes[0] <= 240000)) {
          reportViolation();
          playCount = 0; // reset counter after logging
        }

        // 5. Cooldown Wait 
        activeDelay(5000); 

        // 6. Restore Mics & Flush
        initMic3();
        i2s_start(I2S_NUM_0);
        activeDelay(5000); 

        size_t dummyBytes;
        int32_t dummyBuf[256];
        i2s_read(I2S_NUM_0, dummyBuf, sizeof(dummyBuf), &dummyBytes, 100 / portTICK_PERIOD_MS);
        i2s_read(I2S_NUM_1, dummyBuf, sizeof(dummyBuf), &dummyBytes, 100 / portTICK_PERIOD_MS);
        
        hitCount = 0; 
        
      } else {
        delay(200); 
      }
    } else {
      delay(20); 
    }
  } else {
      delay(500); 
  }
}
