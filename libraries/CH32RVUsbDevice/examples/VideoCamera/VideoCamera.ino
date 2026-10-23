// en: Setup and usage: README.md. / ja: 配線・使い方: README.ja.md。

#include <CH32RVUsbDevice.h>

CH32RVUsbDevice device;
CH32RVUsbDeviceVideo Camera(device, "CH32RVUsbDevice Camera");

static const uint16_t kWidth = 80;
static const uint16_t kHeight = 60;
static const uint8_t kFrameRate = 10;
static const uint32_t kFrameBytes = (uint32_t)kWidth * kHeight * 2;

static volatile uint32_t frameCount = 0;
static uint32_t drawingFrame = 0;
static uint32_t nextFrameDueMs = 0;
static bool wasStreaming = false;

// en: BT.601 bars are Y/U/V; each YUY2 pixel pair shares both chroma components.
// ja: BT.601のY/U/V値。YUY2の2 pixelは両方の色差成分を共有する。
static const uint8_t bars[8][3] = {
    {235, 128, 128}, {210, 16, 146}, {170, 166, 16}, {145, 54, 34},
    {106, 202, 222}, {81, 90, 240},  {41, 240, 110}, {16, 128, 128}};

// en: Address bytes as Y0 U Y1 V even when a payload starts mid-pair.
// ja: payloadがpixel対の途中から始まっても、Y0 U Y1 Vの位置を守る。
static uint8_t pixelByte(uint32_t at)
{
  const uint16_t y = (uint16_t)(at / (kWidth * 2));
  const uint16_t column = (uint16_t)(at % (kWidth * 2));
  const uint16_t x = (uint16_t)((column / 4) * 2);
  const uint8_t component = (uint8_t)(column & 3);
  const uint8_t *bar = bars[(x / (kWidth / 8)) & 7];
  uint8_t luma = bar[0];
  uint8_t u = bar[1];
  uint8_t v = bar[2];
  if (y >= (kHeight * 3) / 4)
  {
    const uint16_t blockWidth = kWidth / 16;
    const uint16_t blockX = (uint16_t)((drawingFrame % 16) * blockWidth);
    luma = (x >= blockX && x < blockX + blockWidth) ? 235 : 16;
    u = 128;
    v = 128;
  }
  return component == 1 ? u : component == 3 ? v : luma;
}

// en: Provider called during USB processing; fill only this requested range and return.
// ja: USB処理中のprovider。要求された範囲だけを埋めて戻る。
static void fillPayload(uint8_t *dst, size_t length, uint32_t offset)
{
  for (size_t i = 0; i < length; i++)
  {
    dst[i] = pixelByte(offset + (uint32_t)i);
  }
}

// en: Completion is delivered by deliverEvents(), not from the payload provider.
// ja: 完了通知はpayload生成中ではなくdeliverEvents()で受ける。
static void frameComplete()
{
  frameCount = frameCount + 1;
}

static void hostCommitted(const CH32RVUsbDeviceVideoCommit &commit)
{
  Serial.printf("Host committed: frame %u, %lu ns interval, payload %lu bytes\n",
                commit.frameIndex,
                (unsigned long)commit.frameInterval * 100,
                (unsigned long)commit.maxPayloadTransferSize);
}

void setup()
{
  Serial.begin(115200, SERIAL_8N1, RX, TX);
  delay(1500);

  Camera.setFormat(CH32RVUsbDeviceVideoFormat::Yuy2);
  Camera.setFrameSize(kWidth, kHeight);
  Camera.setFrameRate(kFrameRate);
  Camera.onFillPayload(fillPayload);
  Camera.onFrameComplete(frameComplete);
  Camera.onCommit(hostCommitted);

  CH32RVUsbDeviceConfig config;
  config.vid = 0x1209;
  config.pid = 0x0001;
  config.manufacturer = "ch32-riscv-ug";
  config.product = "CH32RVUsbDevice Camera";
  config.serialNumber = "camera-1";
  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }
  Serial.printf("UVC camera ready: %ux%u YUY2 at %u fps, %s, %u-byte payloads\n",
                kWidth, kHeight, kFrameRate,
                Camera.bulkStreaming() ? "bulk" : "isochronous",
                (unsigned)Camera.payloadBufferSize());
  Serial.println("Open it wherever your OS lists webcams.");
}

void loop()
{
  // en: Deliver notifications here. / ja: 通知callbackはここで実行する。
  deliverEvents();
  const bool streaming = Camera.streaming();
  if (streaming && !wasStreaming)
  {
    Serial.println("Streaming started");
    nextFrameDueMs = millis();
    frameCount = 0;
  }
  else if (!streaming && wasStreaming)
  {
    Serial.printf("Streaming stopped after %lu frames\n", (unsigned long)frameCount);
  }
  wasStreaming = streaming;

  // en: Pace frames here, not from completion, to honor the advertised rate.
  // ja: 完了直後に連投せず、広告したframe rateに合わせてここで投入する。
  if (streaming && !Camera.frameInFlight() &&
      (int32_t)(millis() - nextFrameDueMs) >= 0)
  {
    nextFrameDueMs += 1000UL / kFrameRate;
    if ((int32_t)(millis() - nextFrameDueMs) > 0)
    {
      // en: After a long stall, avoid a burst of catch-up frames. / ja: 長く遅れた後は、追いつくための連続送信を避ける。
      nextFrameDueMs = millis();
    }
    drawingFrame = frameCount;
    Camera.sendGeneratedFrame(kFrameBytes);
  }
  delay(1);
}
