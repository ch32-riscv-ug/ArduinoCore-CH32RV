// en: Return USB playback audio through the USB recording stream.
// ja: USBで受けた再生音声を、録音側のストリームへ返す。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。
/* requires: flash=48K */

#include <CH32RVUsbDevice.h>

// en: The device groups USB settings; the objects declared below add functions for the host.
// ja: deviceがUSB全体をまとめ、下のオブジェクト宣言でホストに見せる機能を追加する。
CH32RVUsbDevice device;
CH32RVUsbAudioFunction audio(device);
CH32RVUsbAudioPlaybackStream &playback = audio.addPlaybackStream();
CH32RVUsbAudioCaptureStream &capture = audio.addCaptureStream();

void setup()
{
  // en: This Serial is hardware UART for diagnostics, separate from the USB connection being demonstrated.
  // ja: このSerialはログ用のハードウェアUART。ここで試すUSB通信の接続とは別。
  Serial.begin(115200, SERIAL_8N1, RX, TX);
  delay(1500);

  playback.addFormat({32000, 1, 2, 16});
  // en: Use the same PCM format in both directions for direct loopback.
  // ja: 変換せずに返すため、両方向のPCM形式をそろえる。
  capture.addFormat({32000, 1, 2, 16});

  CH32RVUsbDeviceConfig config;
  config.vid = 0x1209;
  config.pid = 0x0001;
  config.manufacturer = "ch32-riscv-ug";
  config.product = "CH32RVUsbDevice Headset";
  config.serialNumber = "ch32rv-headset";

  // en: Start all configured functions together; success starts the stack, while the host configures it afterward.
  // ja: 設定した機能をまとめて開始する。成功は内部の開始を示し、ホストの認識はその後に進む。
  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }
  Serial.println("Audio headset ready: UAC1, 32000 Hz, 16-bit, mono");
}

void loop()
{
  uint8_t pcm[128];
  // en: read() returns bytes actually available, which may be fewer than the array size or zero.
  // ja: read()は実際に得られたバイト数を返す。配列の大きさより少ない場合や0の場合もある。
  const size_t received = playback.read(pcm, sizeof(pcm));
  if (received)
  {
    // en: Forward actual received bytes; partial writes need extra buffering in an application.
    // ja: 実受信分だけ返す。実アプリでは部分書き込みの保留bufferも用意する。
    capture.write(pcm, received);
  }

  CH32RVUsbAudioEvent event;
  while (audio.pollEvent(event))
  {
    if (event.type == CH32RVUsbAudioEventType::StreamStateChanged)
    {
      Serial.printf("AUDIO_STREAM target=%u enabled=%u alt=%u\n",
                    static_cast<unsigned>(event.target),
                    event.enabled ? 1 : 0, event.alternateSetting);
    }
  }
  delay(1);
}
