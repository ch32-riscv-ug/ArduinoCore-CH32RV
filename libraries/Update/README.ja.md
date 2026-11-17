# Update

English: [README.md](README.md)

動いているスケッチを、受け取った新しいイメージで置き換えます。arduino-esp32 の `Update` と同じ名前・エラー番号です。
運ぶ手段（UART、USB の CDC やベンダー、ネットワーク）はスケッチが選びます。USB の DFU と UF2 のドライブは
CH32RVUsbDevice の `CH32RVUsbDeviceDfu` / `CH32RVUsbDeviceMscFirmwareDisk` が同じ仕組みで受けます。

```cpp
#include <CH32RV.h>
#include <Update.h>

if (!Update.begin(size)) { Update.printError(Serial); }
Update.write(buffer, n);          // 順に、何度でも
if (Update.end()) {
    CH32RV.restart();             // ここで入れ替わる（Update.restart() でも同じ）
}
```

## 仕組み

- イメージは flash のスケッチの後ろ（staging）に書きます。`Update.capacity()` が受け取れる最大で、ほぼ
  「flash − 今のスケッチ」です。更新を受けるスケッチは flash の半分以下にします。`CH32RV_FLASH_STORE` の領域は残ります。
- `end()` までに確かめます: 書いたページの読み返し、先頭がこのコアのリセットの入口であること、長さ、staging の
  CRC-32 が渡したバイト列と一致すること。通らなければ何も変えずに false です。
- 入れ替えは再起動のとき（`CH32RV.restart()` / `Update.restart()`）に、RAM から割り込みを止めて先頭へコピーします。
  ほかの再起動（電源、ウォッチドッグ、NRST）では入れ替わらず、今のスケッチで起動します。
- コピーの間（64 KB でおよそ 1 秒）に電源が切れると、スケッチは書きかけになります。工場ブートローダーとデバッグ線は
  そのまま使えるので、そこから直せます。
- USB デバイスを使っているスケッチは、`USBDevice.end()` してから再起動します（コピーの間は応答しません）。

詳しくは [USB ガイドのファームウェア更新](../../docs/usb.ja.md#ファームウェア更新dfu-のダウンロードuf2-のドライブ) と
コアの `ch32rv_update.h` にあります。

## arduino-esp32 との違い

| arduino-esp32 | ここ |
|---|---|
| `begin(size, command, ledPin, ledOn, label)` | 同じ。`command` は `U_FLASH` だけ（ほかは `UPDATE_ERROR_BAD_ARGUMENT`）。`label` は区画が無いので無視 |
| `write()`、`writeStream()`、`end(evenIfRemaining)`、`abort()`、`printError()`、`errorString()`、`getError()`、`hasError()`、`isRunning()`、`isFinished()`、`size()`、`progress()`、`remaining()`、`onProgress()` | 同じ |
| `setMD5()`、`md5String()` | 無い（staging の CRC-32 で確かめる。`crc32()`） |
| `canRollBack()`、`rollBack()` | 前のイメージを残さないので false |
| — | `capacity()`、`beginRandomAccess()` / `writeAt()`（順不同、UF2 用）、`isPending()` / `cancelPending()`、`restart()`、`UPDATE_ERROR_VERIFY`（20） |

## 例

- [SerialUpdate](examples/SerialUpdate/README.ja.md): UART で長さとイメージを受け取って入れ替えます。
