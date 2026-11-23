#include <driver/i2s.h>
#include <math.h>
#include <FastLED.h>
#include "audio_data.h" // Your voice message array

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

float calibration_offset = -14.0f;
float user_threshold = 65.0f; 

unsigned long hitTimes[5] = {0, 0, 0, 0, 0}; 
int hitCount = 0;                            

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
      if (!systemActive) {
        // Going to sleep: Turn off screen immediately
        FastLED.clear();
        FastLED.show();
        Serial.println(">>> System Standby Mode: ON <<<");
      } else {
        Serial.println(">>> System Standby Mode: OFF <<<");
      }
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
      thresholdDisplayTimer = millis();
      lastDebounceTime = millis();
    }
  }
  if (digitalRead(BTN_MINUS) == LOW) {
    if ((millis() - lastDebounceTime) > debounceDelay) {
      user_threshold -= 1.0f;
      if (user_threshold < 40.0f) user_threshold = 40.0f; 
      showThresholdOverlay = true;
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
    delay(10); 
  }
}

// ==========================================
// MAIN SETUP
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(BTN_MINUS, INPUT_PULLUP);
  pinMode(BTN_PLUS, INPUT_PULLUP);
  pinMode(BTN_POWER, INPUT_PULLUP);
  pinMode(6, INPUT); 

  FastLED.addLeds<WS2812B, DISP_PIN, GRB>(dispLeds, NUM_DISP_LEDS);
  FastLED.addLeds<WS2812B, RING_PIN, GRB>(ringLeds, NUM_RING_LEDS);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, 2000); 
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.clear();
  FastLED.show();
  
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
}

// ==========================================
// MAIN LOOP
// ==========================================
void loop() {
  
  // ---------- 1. Handle UI & Power Logic ----------
  handleButtonsAndOverlay(false);

  // ---------- 2. Check Standby Transitions ----------
  if (systemActive != lastSystemState) {
    if (!systemActive) {
      // Safely pause the I2S microphone drivers to save power
      i2s_stop(I2S_NUM_0);
      i2s_stop(I2S_NUM_1);
    } else {
      // Wake up the drivers and flush the start-up spikes!
      i2s_start(I2S_NUM_0);
      i2s_start(I2S_NUM_1);
      delay(1500);
      size_t dummyBytes;
      int32_t dummyBuf[256];
      for(int i = 0; i < 5; i++) {
        i2s_read(I2S_NUM_0, dummyBuf, sizeof(dummyBuf), &dummyBytes, 100 / portTICK_PERIOD_MS);
        i2s_read(I2S_NUM_1, dummyBuf, sizeof(dummyBuf), &dummyBytes, 100 / portTICK_PERIOD_MS);
      }
    }
    lastSystemState = systemActive;
  }

  // If we are in standby, skip the entire audio processing loop!
  if (!systemActive) {
    delay(50);
    return;
  }

  // ---------- 3. Read Microphones ----------
  int32_t pairBuf[128];
  size_t bytesRead0 = 0;
  i2s_read(I2S_NUM_0, pairBuf, sizeof(pairBuf), &bytesRead0, portMAX_DELAY);
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
  i2s_read(I2S_NUM_1, mic3Buf, sizeof(mic3Buf), &bytesRead1, portMAX_DELAY);
  int sampleCount1 = bytesRead1 / sizeof(int32_t);
  float msMic3 = 0.0f;
  for (int i = 0; i < sampleCount1; i++) {
    int32_t s = mic3Buf[i] >> 14;
    msMic3 += (float)s * s;
  }
  if (sampleCount1 > 0) { msMic3 /= sampleCount1; }

  // ---------- 4. Calculate Noise Math ----------
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

    // ---------- 5. Normal Visual Update (2Hz Refresh) ----------
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

    // ---------- 6. ALARM TRIGGER LOGIC ----------
    if (!showThresholdOverlay && alarmVal == 100) {
      
      for (int i = 0; i < 4; i++) {
        hitTimes[i] = hitTimes[i+1];
      }
      hitTimes[4] = millis();
      if (hitCount < 5) hitCount++;

      if (hitCount == 5 && (hitTimes[4] - hitTimes[0] <= 20000)) {
        
        Serial.println(">>>> ALARM TRIGGERED! <<<<");

        // FIX: INSTANT VISUAL OVERRIDE! Bypasses the 2Hz timer and forces RED 100 immediately
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

        // 4. Cooldown Wait 
        activeDelay(5000); 

        // 5. Restore Mics & Flush
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