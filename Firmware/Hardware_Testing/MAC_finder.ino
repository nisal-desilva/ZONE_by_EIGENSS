void setup() {
  Serial.begin(115200);
  delay(1000);

  // Read the 64-bit MAC directly from the eFuse
  uint64_t chipid = ESP.getEfuseMac(); 
  
  Serial.print("Factory eFuse MAC: ");
  // Format it properly as a MAC address
  Serial.printf("%02X:%02X:%02X:%02X:%02X:%02X\n",
                (uint8_t)(chipid),
                (uint8_t)(chipid >> 8),
                (uint8_t)(chipid >> 16),
                (uint8_t)(chipid >> 24),
                (uint8_t)(chipid >> 32),
                (uint8_t)(chipid >> 40));
}

void loop() {
}
