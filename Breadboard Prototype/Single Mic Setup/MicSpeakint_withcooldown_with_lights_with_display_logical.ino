#include <driver/i2s.h>
#include <math.h>
#include <TM1637Display.h>
#include "audio_data.h"

// --- SPEAKER PINS (Highway 1) ---
#define I2S_SPK_BCLK 26
#define I2S_SPK_LRC  25
#define I2S_SPK_DOUT 22

// --- MICROPHONE PINS (Highway 0) ---
#define I2S_MIC_SCK 32
#define I2S_MIC_WS  33
#define I2S_MIC_SD  34

// --- VISUAL DASHBOARD (LEDs + Display) ---
#define LED_1 13 
#define LED_2 12 
#define LED_3 14 
#define CLK 18
#define DIO 19
TM1637Display display(CLK, DIO);

// --- SONOLITE CONFIGURATION ---
float calibration_offset = -14.0; 
float trigger_threshold = 70.0;   
int volume_multiplier = 4;        

// --- SMART SENSOR VARIABLES ---
const int HISTORY_SIZE = 50;      
float db_history[HISTORY_SIZE] = {0};
int history_index = 0;
bool buffer_filled = false;

// --- TIMERS ---
unsigned long last_warning_time = 0;
const unsigned long COOLDOWN_MS = 10000; 

unsigned long last_display_time = 0;
const unsigned long DISPLAY_REFRESH_MS = 500; 

// --- THE PENALTY TRACKER ---
bool penalty_history[10] = {false}; // Stores the last 10 seconds of data
int penalty_index = 0;
unsigned long last_penalty_check = 0;
const unsigned long PENALTY_INTERVAL_MS = 1000; // Check once per second


// ---------------------------------------------------------
// HARDWARE HANDOFF SETUP
// ---------------------------------------------------------

void initMic() {
  i2s_config_t mic_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = 16000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 512,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };
  i2s_pin_config_t mic_pins = {
    .bck_io_num = I2S_MIC_SCK,
    .ws_io_num = I2S_MIC_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_MIC_SD
  };
  i2s_driver_install(I2S_NUM_0, &mic_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &mic_pins);
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
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };
  i2s_pin_config_t spk_pins = {
    .bck_io_num = I2S_SPK_BCLK,
    .ws_io_num = I2S_SPK_LRC,
    .data_out_num = I2S_SPK_DOUT,
    .data_in_num = I2S_PIN_NO_CHANGE
  };
  i2s_driver_install(I2S_NUM_1, &spk_config, 0, NULL);
  i2s_set_pin(I2S_NUM_1, &spk_pins);
}

// ---------------------------------------------------------
// MAIN LOGIC
// ---------------------------------------------------------

void setup() {
  Serial.begin(115200);
  
  pinMode(LED_1, OUTPUT);
  pinMode(LED_2, OUTPUT);
  pinMode(LED_3, OUTPUT);
  digitalWrite(LED_1, LOW);
  digitalWrite(LED_2, LOW);
  digitalWrite(LED_3, LOW);

  display.setBrightness(0x0f); 
  
  Serial.println("Sonolite MVP Ready: Booting Ears...");
  initMic();
}

void loop() {
  int32_t samples[64];
  size_t bytes_read = 0;

  i2s_read(I2S_NUM_0, &samples, sizeof(samples), &bytes_read, portMAX_DELAY);
  int samples_read = bytes_read / sizeof(int32_t);

  if (samples_read > 0) {
    float mean_square = 0;
    for (int i = 0; i < samples_read; i++) {
      int32_t sample = samples[i] >> 14;
      mean_square += (sample * sample);
    }
    mean_square /= samples_read;

    if(mean_square > 0) {
       float current_decibels = 10.0 * log10(mean_square) + calibration_offset;
       
       db_history[history_index] = current_decibels;
       history_index++;
       if (history_index >= HISTORY_SIZE) {
           history_index = 0;
           buffer_filled = true;
       }

       float db_sum = 0;
       int count = buffer_filled ? HISTORY_SIZE : history_index; 
       for (int i = 0; i < count; i++) {
           db_sum += db_history[i];
       }
       float average_db = db_sum / count;
       
       bool is_cooldown = (millis() - last_warning_time < COOLDOWN_MS);

       // --- THE FAST UX: LED VU METER ---
       if (is_cooldown) {
           digitalWrite(LED_1, LOW);
           digitalWrite(LED_2, LOW);
           digitalWrite(LED_3, LOW);
       } else {
           digitalWrite(LED_1, (average_db > 60.0) ? HIGH : LOW);
           digitalWrite(LED_2, (average_db > 65.0) ? HIGH : LOW);
           digitalWrite(LED_3, (average_db > trigger_threshold) ? HIGH : LOW);
       }

       // --- THE SLOW UX: NUMBERS & GRAPH ---
       if (millis() - last_display_time >= DISPLAY_REFRESH_MS) {
           Serial.print("Raw:");
           Serial.print(current_decibels);
           Serial.print(",Avg:");
           Serial.println(average_db);

           display.showNumberDec((int)average_db, false);
           last_display_time = millis(); 
       }

       // --- THE NEW LOGIC: 10-SECOND PENALTY TRACKER ---
       if (millis() - last_penalty_check >= PENALTY_INTERVAL_MS) {
           
           // 1. Give a penalty if the room is too loud right now
           penalty_history[penalty_index] = (buffer_filled && average_db > trigger_threshold);
           
           // 2. Move to the next second in our 10-second memory loop
           penalty_index++;
           if (penalty_index >= 10) penalty_index = 0;

           // 3. Count up the total penalties over the last 10 seconds
           int total_penalties = 0;
           for (int i = 0; i < 10; i++) {
               if (penalty_history[i] == true) {
                   total_penalties++;
               }
           }

           // 4. Trigger the alarm ONLY if they hit 5 penalties
           if (total_penalties >= 5 && !is_cooldown) {
              Serial.println(">>> 5 SECONDS OF LOUD NOISE DETECTED! TRIGGERING! <<<");
              
              i2s_driver_uninstall(I2S_NUM_0);
              initSpeaker();
              playWarningMessage();
              delay(2000); 
              
              i2s_driver_uninstall(I2S_NUM_1);
              initMic();
              
              // Wipe all memory clean so we start fresh
              buffer_filled = false;
              history_index = 0;
              for(int i=0; i<HISTORY_SIZE; i++) db_history[i] = 0; 
              
              for(int i=0; i<10; i++) penalty_history[i] = false; // Wipe the penalties!
              
              last_warning_time = millis(); 
           }
           
           last_penalty_check = millis(); // Reset the 1-second stopwatch
       }
    }
  }
  delay(10);
}

// ---------------------------------------------------------
// AUDIO PLAYBACK
// ---------------------------------------------------------

void playWarningMessage() {
  size_t bytes_written;
  int16_t* audio_samples = (int16_t*)(audiomass_output + 44);
  size_t sample_count = (sizeof(audiomass_output) - 44) / 2;

  const int CHUNK_SIZE = 256; 
  int16_t chunk_buffer[CHUNK_SIZE];

  for (size_t i = 0; i < sample_count; i += CHUNK_SIZE) {
    int current_chunk = (sample_count - i < CHUNK_SIZE) ? (sample_count - i) : CHUNK_SIZE;

    for (int j = 0; j < current_chunk; j++) {
      int32_t amplified_sample = audio_samples[i + j] * volume_multiplier;
      if (amplified_sample > 32767) amplified_sample = 32767;
      if (amplified_sample < -32768) amplified_sample = -32768;
      chunk_buffer[j] = (int16_t)amplified_sample;
    }

    i2s_write(I2S_NUM_1, chunk_buffer, current_chunk * 2, &bytes_written, portMAX_DELAY);
  }
}