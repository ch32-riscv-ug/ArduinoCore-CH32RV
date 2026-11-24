// en: AES-128 by the H41x encryption module: FIPS-197's example block encrypted and decrypted, then a message in CTR.
// ja: H41x の暗号器で AES-128: FIPS-197 の例のブロックを暗号化して復号し、次にメッセージを CTR で暗号化する。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

/* requires: CH32RV_ECDC_BASE */

#include <CH32RVEcdc.h>

CH32RVEcdc ecdc;

static void print(const char *label, const uint8_t *data, size_t len) {
  Serial.print(label);
  for (size_t i = 0; i < len; i++) {
    if (data[i] < 0x10) {
      Serial.print('0');
    }
    Serial.print(data[i], HEX);
  }
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  const uint8_t key[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                           0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
  const uint8_t plain[16] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                             0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
  uint8_t cipher[16], back[16];
  ecdc.begin(CH32RVEcdcAlgorithm::Aes, key, sizeof key);
  ecdc.encrypt(plain, cipher, 16);
  // en: FIPS-197 C.1: 69C4E0D86A7B0430D8CDB78070B4C55A
  // ja: FIPS-197 の C.1: 69C4E0D86A7B0430D8CDB78070B4C55A
  print("ECB encrypt: ", cipher, 16);
  ecdc.decrypt(cipher, back, 16);
  print("ECB decrypt: ", back, 16);

  // en: CTR takes any length; the same call with the same counter decrypts.
  // ja: CTR は長さを問わない。同じカウンタで同じ呼び出しをすると復号になる。
  const char message[] = "Hello from the ECDC";
  uint8_t iv[16] = {0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
                    0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff};
  uint8_t sealed[sizeof message], opened[sizeof message];
  ecdc.ctr(iv, (const uint8_t *)message, sealed, sizeof message);
  print("CTR encrypt: ", sealed, sizeof message);
  ecdc.ctr(iv, sealed, opened, sizeof message);
  Serial.print("CTR decrypt: ");
  Serial.println((const char *)opened);
}

void loop() {
}
