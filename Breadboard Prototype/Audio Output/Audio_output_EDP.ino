#include <driver/i2s.h>
#include "audio_data.h" // This pulls in your massive hex array

// The I2S Pins
#define I2S_BCLK 26
#define I2S_LRC  25
#define I2S_DOUT 22

void setup() {
  Serial.begin(115200);

  // Configure the I2S driver for 44.1kHz, 16-bit Mono
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = 44100, // Matches your AudioMass export
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

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCLK,
    .ws_io_num = I2S_LRC,
    .data_out_num = I2S_DOUT,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
  
  Serial.println("Starting local playback...");
}


void loop() {
  size_t bytes_written;
  
  // 1. Set your volume multiplier here (Try 2, 4, 8, or 16)
  int volume_multiplier = 8; 
  
  // 2. Convert the raw bytes into 16-bit audio samples
  int16_t* audio_samples = (int16_t*)(audiomass_output + 44);
  size_t sample_count = (sizeof(audiomass_output) - 44) / 2;
  
  Serial.println("Playing amplified message...");

  // 3. Loop through the audio and multiply the volume on the fly
  for (size_t i = 0; i < sample_count; i++) {
    
    // Multiply the soundwave
    int32_t amplified_sample = audio_samples[i] * volume_multiplier;
    
    // Safety limit: Prevent the math from exceeding the 16-bit limit (which causes horrible screeching)
    if (amplified_sample > 32767) amplified_sample = 32767;
    if (amplified_sample < -32768) amplified_sample = -32768;

    // Send the louder sample to the amplifier
    int16_t final_sample = (int16_t)amplified_sample;
    i2s_write(I2S_NUM_0, &final_sample, sizeof(int16_t), &bytes_written, portMAX_DELAY);
  }
  
  Serial.println("Waiting 5 seconds...");
  delay(5000); 
}