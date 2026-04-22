#include <driver/i2s.h>
#include <math.h>

// -------------------------
// I2S0 : Mic 1 + Mic 2
// -------------------------
#define I2S0_MIC_SCK  6
#define I2S0_MIC_WS   7
#define I2S0_MIC_SD   15

// -------------------------
// I2S1 : Mic 3
// -------------------------
#define I2S1_MIC_SCK  18
#define I2S1_MIC_WS   17
#define I2S1_MIC_SD   8

// -------------------------
// Configuration
// -------------------------
float calibration_offset = -14.0f;

float user_threshold = 0.0f;
bool threshold_set = false;

// -------------------------
// Init Mic Pair (Mic1 + Mic2)
// -------------------------
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

// -------------------------
// Init Mic 3
// -------------------------
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
    .bck_io_num = I2S1_MIC_SCK,
    .ws_io_num = I2S1_MIC_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S1_MIC_SD
  };

  i2s_driver_install(I2S_NUM_1, &cfg, 0, NULL);
  i2s_set_pin(I2S_NUM_1, &pins);
}

float rmsToDb(float mean_square) {
  if (mean_square <= 0.0f) return -999.0f;
  return 10.0f * log10(mean_square) + calibration_offset;
}

void printPercentageResult(const char* label, float dB) {
  float percentage = (dB / user_threshold) * 100.0f;
  if (percentage < 0.0f) percentage = 0.0f;

  Serial.print(label);
  Serial.print(": ");

  if (percentage > 100.0f) {
    Serial.print("Threshold exceeded");
  } else {
    Serial.print((int)percentage);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("3-Mic Threshold Test");
  initMicPair();
  initMic3();

  Serial.println("=================================");
  Serial.println("Type the threshold level (dB) and press Enter:");
}

void loop() {
  // Wait for threshold input first
  if (!threshold_set) {
    if (Serial.available() > 0) {
      user_threshold = Serial.parseFloat();

      while (Serial.available() > 0) {
        Serial.read();
      }

      if (user_threshold > 0.0f) {
        threshold_set = true;
        Serial.print("Threshold locked at: ");
        Serial.println(user_threshold);
        Serial.println("Monitoring started...");
        Serial.println("=================================");
      }
    }
    return;
  }

  // ---------- Read Mic1 + Mic2 stereo pair ----------
  int32_t pairBuf[128];
  size_t bytesRead0 = 0;
  i2s_read(I2S_NUM_0, pairBuf, sizeof(pairBuf), &bytesRead0, portMAX_DELAY);

  int sampleCount0 = bytesRead0 / sizeof(int32_t);
  int frameCount = sampleCount0 / 2;

  float msMic1 = 0.0f;
  float msMic2 = 0.0f;

  for (int i = 0; i < frameCount; i++) {
    // If Mic1 and Mic2 appear swapped, swap these two assignments
    int32_t leftSample  = pairBuf[2 * i]     >> 14;
    int32_t rightSample = pairBuf[2 * i + 1] >> 14;

    msMic1 += (float)leftSample * (float)leftSample;
    msMic2 += (float)rightSample * (float)rightSample;
  }

  if (frameCount > 0) {
    msMic1 /= frameCount;
    msMic2 /= frameCount;
  }

  // ---------- Read Mic3 ----------
  int32_t mic3Buf[64];
  size_t bytesRead1 = 0;
  i2s_read(I2S_NUM_1, mic3Buf, sizeof(mic3Buf), &bytesRead1, portMAX_DELAY);

  int sampleCount1 = bytesRead1 / sizeof(int32_t);
  float msMic3 = 0.0f;

  for (int i = 0; i < sampleCount1; i++) {
    int32_t s = mic3Buf[i] >> 14;
    msMic3 += (float)s * (float)s;
  }

  if (sampleCount1 > 0) {
    msMic3 /= sampleCount1;
  }

  float dB1 = rmsToDb(msMic1);
  float dB2 = rmsToDb(msMic2);
  float dB3 = rmsToDb(msMic3);

  printPercentageResult("Mic1", dB1);
  Serial.print(" | ");
  printPercentageResult("Mic2", dB2);
  Serial.print(" | ");
  printPercentageResult("Mic3", dB3);
  Serial.println();

  delay(100);
}
