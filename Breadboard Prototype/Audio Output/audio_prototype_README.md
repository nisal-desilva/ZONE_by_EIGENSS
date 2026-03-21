# Audio Output Subsystem

**Objective:** Validate the I2S audio output capabilities using a standard ESP32, an external I2S DAC/Amplifier, and a 3W speaker. This prototype plays a custom `.wav` voice warning entirely offline by storing the raw audio data in the ESP32's PROGMEM.

## 🛠️ Hardware Requirements
* **Microcontroller:** Standard ESP32 DevKitC (WROOM-32)
* **Amplifier:** MAX98357A I2S Class D Amplifier
* **Speaker:** 4 Ohm, 3W 
* **Power:** 5V External Power Supply (To prevent USB brownouts)

## 🔌 Wiring Diagram
**CRITICAL SAFETY NOTE:** The ground of the 5V External Supply MUST be tied to the ground of the ESP32, or the amplifier will not read the data signals correctly.

| MAX98357A Pin | Connection | Notes |
| :--- | :--- | :--- |
| **VIN** | 5V External Supply | Do not power from ESP32 3V3. |
| **GND** | Lab Supply GND + ESP32 GND | **Must share a common ground!** |
| **BCLK** | ESP32 `D26` | Bit Clock |
| **LRC** | ESP32 `D25` | Left/Right Clock (Word Select) |
| **DIN** | ESP32 `D22` | Data In |
| **GAIN** | GND | Hardware boost (15dB) |
| **Speaker Terminals**| 4 Ohm Speaker | Polarity does not matter for mono. |

## 🎵 Audio File Preparation
Because the standard ESP32 lacks the RAM to decode MP3s on the fly, audio must be converted to raw hex arrays.
1. Format your audio file using [AudioMass](https://audiomass.co): Export as **WAV**, **44100Hz**, **Mono**, **16-bit**.
2. Convert the WAV to a C-Array using [FileToCArray](https://notisrac.github.io/FileToCArray/): Check "Treat as binary" and "PROGMEM".
3. Save the output into a new IDE tab named `audio_data.h`.

## 💻 The Code (`main.ino`)
*Note: This code includes a software DSP multiplier to artificially boost the volume of quiet WAV files.*

```cpp
#include <driver/i2s.h>
#include "audio_data.h" // Your generated hex array

// I2S Pins
#define I2S_BCLK 26
#define I2S_LRC  25
#define I2S_DOUT 22

void setup() {
  Serial.begin(115200);

  // Configure I2S driver for 44.1kHz, 16-bit Mono
  i2s_config_t i2s_config = {
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

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCLK,
    .ws_io_num = I2S_LRC,
    .data_out_num = I2S_DOUT,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
}

void loop() {
  size_t bytes_written;
  int volume_multiplier = 4; // Increase if audio is too quiet
  
  // Point to the data inside the array (skip the 44-byte WAV header)
  int16_t* audio_samples = (int16_t*)(audiomass_output + 44);
  size_t sample_count = (sizeof(audiomass_output) - 44) / 2;
  
  Serial.println("Playing amplified message...");

  for (size_t i = 0; i < sample_count; i++) {
    // Software DSP Volume Boost
    int32_t amplified_sample = audio_samples[i] * volume_multiplier;
    
    // Prevent clipping/screeching
    if (amplified_sample > 32767) amplified_sample = 32767;
    if (amplified_sample < -32768) amplified_sample = -32768;

    int16_t final_sample = (int16_t)amplified_sample;
    i2s_write(I2S_NUM_0, &final_sample, sizeof(int16_t), &bytes_written, portMAX_DELAY);
  }
  
  delay(5000); 
}
