/* Wiring, operation and expected output: README.md / README.ja.md. */
/* VideoCamera - ported from EspUsbDevice's example of the same name (renamed
 * EspUsbDevice -> CH32RVUsbDevice, ESP_USB_ -> CH32RV_USB_). Controller support and wiring: see the adjacent README files.
 * docs/usb.ja.md describes the library.
 *
 * A USB Video Class camera with no camera: it streams a generated test
 * pattern, so it runs on a bare board. Windows, macOS and Linux bind their
 * own UVC driver with nothing to install.
 *
 * Differences from the ESP32 version: the frames are 80x60 instead of 160x120,
 * and there is no frame buffer - sendGeneratedFrame() asks fillPayload() for
 * the bytes of each payload as it goes out, so the picture costs no RAM. A
 * sketch with real pixels in memory uses sendFrame(buffer, length) exactly as
 * on the ESP32. Full speed on USBFS streams bulk by default (tusb_config.h);
 * with -DCFG_TUD_VIDEO_STREAMING_BULK=0 in build_opt.h it is isochronous at
 * 64 bytes per millisecond, about 6 fps of this picture.
 */
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

/* BT.601 colour bars as {Y, U, V}. YUY2 carries one U and one V per pair of
 * pixels and both matter: hold V at 128 and the picture still looks like a
 * plausible test pattern with every colour wrong. */
static const uint8_t bars[8][3] = {
    {235, 128, 128}, {210, 16, 146}, {170, 166, 16}, {145, 54, 34},
    {106, 202, 222}, {81, 90, 240},  {41, 240, 110}, {16, 128, 128}};

/* One byte of the frame: colour bars with a white block that steps across the
 * bottom once per frame. The moving block is the point: a still image cannot
 * tell a live stream from a stuck one. YUY2 packs two pixels into four bytes,
 * Y0 U Y1 V, and a payload may start on any of them. */
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

/* Runs on the USB task for each payload: `length` bytes from `offset`. */
static void fillPayload(uint8_t *dst, size_t length, uint32_t offset)
{
  for (size_t i = 0; i < length; i++)
  {
    dst[i] = pixelByte(offset + (uint32_t)i);
  }
}

/* Runs on the USB task when the last payload of a frame has gone. Keep it
 * short; this one only counts. */
static void frameComplete()
{
  frameCount = frameCount + 1;
}

/* The host negotiates before it starts, and what it committed to can differ
 * from what was advertised. */
static void hostCommitted(const CH32RVUsbDeviceVideoCommit &commit)
{
  Serial.printf("Host committed: frame %u, %lu ns interval, payload %lu bytes\n",
                commit.frameIndex,
                (unsigned long)commit.frameInterval * 100,
                (unsigned long)commit.maxPayloadTransferSize);
}

void setup()
{
  Serial.begin(115200, SERIAL_8N1, RX, TX);   /* the log: RX, TX are the pads you wired */
  delay(1500);

  Camera.setFormat(CH32RVUsbDeviceVideoFormat::Yuy2);
  Camera.setFrameSize(kWidth, kHeight);
  Camera.setFrameRate(kFrameRate);
  Camera.onFillPayload(fillPayload);
  Camera.onFrameComplete(frameComplete);
  Camera.onCommit(hostCommitted);

  CH32RVUsbDeviceConfig config;
  config.vid = 0x1209;   /* pid.codes test ID: a product needs its own */
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
                CH32RVUsbDeviceVideo::bulkStreaming() ? "bulk" : "isochronous",
                (unsigned)CH32RVUsbDeviceVideo::payloadBufferSize());
  Serial.println("Open it wherever your OS lists webcams.");
}

void loop()
{
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

  /* One place arms frames, paced to the advertised rate. Arming from the
   * completion callback instead would send as fast as the endpoint drains,
   * which is not the frame rate the host was promised. */
  if (streaming && !Camera.frameInFlight() &&
      (int32_t)(millis() - nextFrameDueMs) >= 0)
  {
    nextFrameDueMs += 1000UL / kFrameRate;
    if ((int32_t)(millis() - nextFrameDueMs) > 0)
    {
      /* A long stall would otherwise leave the deadline behind and send a
       * burst to catch up. */
      nextFrameDueMs = millis();
    }
    drawingFrame = frameCount;
    Camera.sendGeneratedFrame(kFrameBytes);
  }
  delay(1);
}
