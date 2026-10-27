#include "st.h"
// Version helper on hardware: print whether the stored struct was accepted, then save it with a boot count.
struct Cfg { uint32_t boots; uint16_t b; uint8_t c, d; };
void setup() {
  Serial.begin(115200); delay(300);
  Cfg c; const bool ok = st_load(&c, sizeof c);
  if (!ok) c = {0, 50, 3, 1};
  c.boots++; st_save(&c, sizeof c);
  Serial.print("kind "); Serial.print(ST_KIND); Serial.print(" ver "); Serial.print(ST_VERSION);
  Serial.print(ok ? " loaded boots " : " defaults boots "); Serial.print(c.boots); Serial.println(" millis");
}
void loop() {}
