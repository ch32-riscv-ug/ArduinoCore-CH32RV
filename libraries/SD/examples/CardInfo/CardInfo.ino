// en: An SD card over SPI: its type and size, a file written and read back, the root listed, and 256 KB timed each way.
// ja: SPIのSDカード。種類と大きさ、ファイルの書き込みと読み戻し、ルートの一覧、256 KBの書き込みと読み出しの時間。
// en: Wiring and what it prints: README.md.
// ja: 配線と表示される内容: README.ja.md。
/* requires: SPI1, flash=48K */

#include <SD.h>

// en: The card's chip select. SCK, MISO and MOSI are the SPI bus's (SPI.begin()).
// ja: カードのチップセレクト。SCK・MISO・MOSIはSPIのバスのもの（SPI.begin()）。
#ifndef SD_CS_PIN
#ifdef SS
#define SD_CS_PIN SS
#else
#define SD_CS_PIN PA4   // en: a series without SS (X035): any free pin / ja: SSの無い系列（X035）: 空いているどのピンでも
#endif
#endif

static const char *typeName(sdcard_type_t t)
{
  switch (t)
  {
  case CARD_MMC: return "MMC";
  case CARD_SD: return "SDSC";
  case CARD_SDHC: return "SDHC/SDXC";
  default: return "none";
  }
}

static uint8_t block[4096];

static void timeIt(FS &fs)
{
  for (size_t i = 0; i < sizeof(block); i++)
  {
    block[i] = (uint8_t)i;
  }
  uint32_t t0 = millis();
  File f = fs.open("/speed.bin", FILE_WRITE);
  for (int i = 0; i < 64; i++)
  {
    f.write(block, sizeof(block));
  }
  f.close();
  const uint32_t written = millis() - t0;
  t0 = millis();
  f = fs.open("/speed.bin");
  size_t total = 0, n;
  while ((n = f.read(block, sizeof(block))) > 0)
  {
    total += n;
  }
  f.close();
  const uint32_t read = millis() - t0;
  Serial.printf("256 KB: write %lu ms, read %lu ms (%lu bytes back)\n", static_cast<unsigned long>(written),
                static_cast<unsigned long>(read), static_cast<unsigned long>(total));
  fs.remove("/speed.bin");
}

void setup()
{
  Serial.begin(115200);
  delay(1500);

  SPI.begin();
  // en: 4 MHz after identification; raise it (up to 25 MHz) once the wiring proves good.
  // ja: 識別のあとは4 MHz。配線が確かなら（25 MHzまで）上げられる。
  if (!SD.begin(SD_CS_PIN, SPI, 4000000))
  {
    Serial.println("SD_BEGIN_FAILED (no card, no FAT volume, or wiring)");
    return;
  }
  Serial.printf("card %s, %lu MB, FAT type %d, %lu MB free\n", typeName(SD.cardType()),
                static_cast<unsigned long>(SD.cardSize() >> 20), static_cast<int>(SD.fatType()),
                static_cast<unsigned long>(SD.freeBytes() >> 20));

  File f = SD.open("/ch32rv.txt", FILE_WRITE);
  f.printf("written at %lu ms\n", millis());
  f.close();
  f = SD.open("/ch32rv.txt");
  Serial.print("read back: ");
  while (f.available())
  {
    Serial.write(f.read());
  }
  f.close();

  File root = SD.open("/");
  for (File e = root.openNextFile(); e; e = root.openNextFile())
  {
    Serial.printf("  %s%s  %lu\n", e.name(), e.isDirectory() ? "/" : "", static_cast<unsigned long>(e.size()));
  }
  root.close();

  timeIt(SD);
  SD.end();
}

void loop()
{
}
