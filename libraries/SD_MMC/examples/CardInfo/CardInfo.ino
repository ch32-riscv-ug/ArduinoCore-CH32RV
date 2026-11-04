// en: An SD card on the SDIO host: its type and size, a file written and read back, the root listed, and 256 KB timed each way.
// ja: SDIOのSDカード。種類と大きさ、ファイルの書き込みと読み戻し、ルートの一覧、256 KBの書き込みと読み出しの時間。
// en: Wiring and what it prints: README.md.
// ja: 配線と表示される内容: README.ja.md。
/* requires: SDIO, flash=48K */

#include <SD_MMC.h>


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

  // en: The series' default pins, 4 data lines, up to 20 MHz (SD_MMC.setPins() before begin() picks others).
  // ja: 系列の既定のピン、データ線4本、20 MHzまで（ほかのピンはbegin()の前にSD_MMC.setPins()で）。
  if (!SD_MMC.begin())
  {
    Serial.println("SD_MMC_BEGIN_FAILED (no card, no FAT volume, or wiring)");
    return;
  }
  Serial.printf("SDIO_CK %lu kHz, %d bits\n", static_cast<unsigned long>(SD_MMC.card().clockHz() / 1000),
                SD_MMC.card().wide() ? 4 : 1);
  Serial.printf("card %s, %lu MB, FAT type %d, %lu MB free\n", typeName(SD_MMC.cardType()),
                static_cast<unsigned long>(SD_MMC.cardSize() >> 20), static_cast<int>(SD_MMC.fatType()),
                static_cast<unsigned long>(SD_MMC.freeBytes() >> 20));

  File f = SD_MMC.open("/ch32rv.txt", FILE_WRITE);
  f.printf("written at %lu ms\n", millis());
  f.close();
  f = SD_MMC.open("/ch32rv.txt");
  Serial.print("read back: ");
  while (f.available())
  {
    Serial.write(f.read());
  }
  f.close();

  File root = SD_MMC.open("/");
  for (File e = root.openNextFile(); e; e = root.openNextFile())
  {
    Serial.printf("  %s%s  %lu\n", e.name(), e.isDirectory() ? "/" : "", static_cast<unsigned long>(e.size()));
  }
  root.close();

  timeIt(SD_MMC);
  SD_MMC.end();
}

void loop()
{
}
