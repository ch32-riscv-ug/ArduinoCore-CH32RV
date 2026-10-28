// --- power-cut test driver (the store code above is FlashSettingsTwoBanks' own, copied by pc.sh) ---
// Commands (one line each): "i" first save of 1000; "r" report what the last boot found; "g D" wait D ms, then three
// saves of value+1.. back to back (the host cuts the power meanwhile).
static bool bootLoaded, bootA, bootB;
static uint16_t bootGenA, bootGenB;
static Settings cur;

void setup() {
  Serial.begin(115200);
  bootA = crc16(*bank(0)) == bank(0)->f.crc;
  bootB = crc16(*bank(1)) == bank(1)->f.crc;
  bootGenA = bank(0)->f.gen;
  bootGenB = bank(1)->f.gen;
  bootLoaded = loadSettings(cur);
  if (!bootLoaded) cur = kDefaults;
}

void loop() {
  if (!Serial.available()) return;
  const int c = Serial.read();
  if (c == 'i') {
    cur.interval_ms = 1000;
    Serial.print(saveSettings(cur) ? "init ok\n" : "init fail\n");
  } else if (c == 'r') {
    Serial.print("pc loaded "); Serial.print(bootLoaded);
    Serial.print(" value "); Serial.print(cur.interval_ms);
    Serial.print(" a "); Serial.print(bootA); Serial.print(' '); Serial.print(bootGenA);
    Serial.print(" b "); Serial.print(bootB); Serial.print(' '); Serial.println(bootGenB);
  } else if (c == 'g') {
    const long d = Serial.parseInt();
    delay(d);
    for (int k = 0; k < 3; k++) {
      cur.interval_ms++;
      saveSettings(cur);
    }
    Serial.print("done "); Serial.println(cur.interval_ms);
  }
}
