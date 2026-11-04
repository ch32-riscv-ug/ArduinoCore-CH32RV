// en: A FAT volume in RAM: format it, write and append files, list the directories, read a file back.
// ja: RAM上のFATボリューム。フォーマットし、ファイルを書き・追記し、ディレクトリを一覧し、読み戻す。
// en: What it prints: README.md.
// ja: 表示される内容: README.ja.md。
/* requires: ram=48K */

#include <FatFS.h>

// en: 64 blocks (32 KB) is the smallest FAT volume FatFs makes.
// ja: 64ブロック（32 KB）がFatFsで作れる最小のFATボリューム。
static uint8_t storage[64 * 512];
fs::RamBlockDevice disk(storage, sizeof(storage) / 512);
FatFS fat;

static void listDir(FS &fs, const char *path, int depth)
{
  File dir = fs.open(path);
  if (!dir || !dir.isDirectory())
  {
    return;
  }
  // en: e = openNextFile() opens the next entry before the previous one is released, so listing holds three files.
  // ja: e = openNextFile() は前のエントリーを手放す前に次を開くので、一覧の間はファイルを3つ使う。
  for (File e = dir.openNextFile(); e; e = dir.openNextFile())
  {
    for (int i = 0; i < depth; i++)
    {
      Serial.print("  ");
    }
    Serial.printf("%s%s  %u\n", e.name(), e.isDirectory() ? "/" : "", static_cast<unsigned>(e.size()));
    if (e.isDirectory())
    {
      listDir(fs, e.path(), depth + 1);
    }
  }
}

void setup()
{
  Serial.begin(115200);
  delay(1500);

  // en: A RAM disk starts empty, so format it when it holds no volume.
  // ja: RAMディスクは空から始まるので、ボリュームが無ければフォーマットする。
  if (!fat.begin(disk, true))
  {
    Serial.println("MOUNT_FAILED");
    return;
  }
  Serial.printf("FAT type %d, %lu bytes, %lu free\n", static_cast<int>(fat.fatType()),
                static_cast<unsigned long>(fat.totalBytes()), static_cast<unsigned long>(fat.freeBytes()));

  File f = fat.open("/config.json", FILE_WRITE);
  f.print("{\"interval\":10}");
  f.close();

  // en: create = true makes the missing parent directories.
  // ja: create = true で、無い親ディレクトリを作る。
  for (int i = 0; i < 3; i++)
  {
    File log = fat.open("/logs/today.txt", FILE_APPEND, true);
    log.printf("record %d at %lu ms\n", i, millis());
    log.close();
  }

  listDir(fat, "/", 0);

  File in = fat.open("/logs/today.txt");
  Serial.println("--- /logs/today.txt");
  while (in.available())
  {
    Serial.write(in.read());
  }
  in.close();

  Serial.printf("used %lu bytes\n", static_cast<unsigned long>(fat.usedBytes()));
  fat.end();
}

void loop()
{
}
