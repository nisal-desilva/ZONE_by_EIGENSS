# EIGENSS Sonolite MVP (v1.0)

Sonolite is an autonomous, standalone acoustic monitoring system built on the ESP32. It continuously monitors ambient room decibels, filters out transient noises, and provides both visual and auditory feedback to enforce volume control in shared spaces.

## Features
* **Smart DSP Logic:** Uses a 50-tick moving average circular buffer to filter out sudden noises (e.g., dropped books).
* **Debounce Trigger (Leaky Bucket):** Tracks volume over a rolling 10-second window. The alarm only fires if the room exceeds the threshold for a sustained 5 seconds.
* **Half-Duplex Audio Handoff:** Dynamically destroys and re-initializes I2S DMA drivers to prevent clock collisions between the 16kHz microphone and 44.1kHz amplifier.
* **Visual Dashboard:** Real-time 3-stage LED VU meter and a TM1637 digital decibel display (refreshing at 2Hz for readability).
* **Direct Audio Playback:** Pushes raw hex audio data from flash memory directly to a Class-D amplifier with on-the-fly math amplification.

## Hardware Requirements
* ESP32 Development Board
* INMP441 Omnidirectional I2S Microphone
* MAX98357A I2S Class-D Amplifier (with 3W Speaker)
* TM1637 4-Digit Display
* 3x Red LEDs (with current limiting resistors)
* 5V External Power Supply (Shared common ground with ESP32 required)

## Pin Mapping
### I2S Microphone (Highway 0)
* VDD -> 3.3V
* GND -> GND
* L/R -> GND
* SCK -> D32
* WS  -> D33
* SD  -> D34

### I2S Amplifier (Highway 1)
* VIN  -> 5V External
* GND  -> Common GND
* GAIN -> GND (Max Gain)
* BCLK -> D26
* LRC  -> D25
* DIN  -> D22

### Visuals
* LED 1 (60dB) -> D13
* LED 2 (65dB) -> D12
* LED 3 (70dB) -> D14
* TM1637 CLK  -> D18
* TM1637 DIO  -> D19

## Dependencies
* `driver/i2s.h` (Native ESP-IDF)
* `TM1637` by Avishorp (via Arduino Library Manager)
