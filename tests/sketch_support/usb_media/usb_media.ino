/* USB device contracts, media set (tests/instrumented/usb): a UAC1 headset
 * (32 kHz, 16-bit mono both ways) and a UVC camera (80x60 YUY2, bulk), with
 * the default DFU runtime. Content the host can check exactly:
 *   - microphone: consecutive samples count up by one (wrapping at 16 bits)
 *   - camera: byte i of frame f is (i * 7 + f) & 0xFF
 *   - speaker: what arrives is counted and summed for the host to compare
 *
 *   ready <nonce>   READY <nonce> <build id> <USB started 0/1> <last error>
 *   audio           AUDIO <speaker bytes> <speaker byte sum> <mic writes refused>
 *   video           VIDEO <frames sent> <streaming 0/1> */
#include <Arduino.h>
#include <SerialDMSeq.h>
#include <CH32RVUsbDevice.h>
#include <string.h>

#ifndef BENCH_BUILD_ID
#error BENCH_BUILD_ID must identify this compilation
#endif

CH32RVUsbAudioFunction audio;
CH32RVUsbAudioPlaybackStream &speaker = audio.addPlaybackStream();
CH32RVUsbAudioCaptureStream &mic = audio.addCaptureStream();
CH32RVUsbDeviceVideo camera;

static const uint32_t RATE = 32000;
static CH32RVUsbDeviceConfig config;
static uint16_t ramp;
static uint32_t lastMicros, owed, refused;
static uint32_t speakerBytes, speakerSum;
static volatile uint32_t frames;
static uint32_t drawing;
static uint32_t nextFrameMs;
static char line[80];
static uint8_t used;

static void fill(uint8_t *dst, size_t length, uint32_t offset)
{
    for (size_t i = 0; i < length; i++) {
        dst[i] = (uint8_t)((offset + i) * 7u + drawing);
    }
}

static void frameDone()
{
    frames = frames + 1;
}

static void reply(const char *command)
{
    if (strncmp(command, "ready ", 6) == 0) {
        SerialDMSeq.print("READY ");
        SerialDMSeq.print(command + 6);
        SerialDMSeq.print(' ');
        SerialDMSeq.print(BENCH_BUILD_ID);
        SerialDMSeq.print(' ');
        SerialDMSeq.print(USBDevice.started() ? 1 : 0);
        SerialDMSeq.print(' ');
        SerialDMSeq.println(USBDevice.lastErrorName());
    } else if (strcmp(command, "audio") == 0) {
        SerialDMSeq.print("AUDIO ");
        SerialDMSeq.print((unsigned long)speakerBytes);
        SerialDMSeq.print(' ');
        SerialDMSeq.print((unsigned long)speakerSum);
        SerialDMSeq.print(' ');
        SerialDMSeq.println((unsigned long)refused);
    } else if (strcmp(command, "video") == 0) {
        SerialDMSeq.print("VIDEO ");
        SerialDMSeq.print((unsigned long)frames);
        SerialDMSeq.print(' ');
        SerialDMSeq.println(camera.streaming() ? 1 : 0);
    } else {
        SerialDMSeq.println("ERROR COMMAND");
    }
}

void setup()
{
    SerialDMSeq.begin(115200);
    speaker.addFormat({RATE, 1, 2, 16});
    mic.addFormat({RATE, 1, 2, 16});
    camera.setFormat(CH32RVUsbDeviceVideoFormat::Yuy2);
#ifdef USB_MEDIA_ISOCHRONOUS
    camera.setTransferType(CH32RVUsbDeviceVideoTransfer::Isochronous);
#endif
    camera.setFrameSize(80, 60);
    camera.setFrameRate(10);
    camera.onFillPayload(fill);
    camera.onFrameComplete(frameDone);
    config.product = "CH32RV usb-media " BENCH_BUILD_ID;
    config.selfPowered = true;
    USBDevice.begin(config);
    lastMicros = micros();
}

void loop()
{
    deliverEvents();   /* the callbacks set in setup() run here */
    /* Microphone: as many samples as real time asks for. */
    const uint32_t now = micros();
    uint32_t elapsed = now - lastMicros;
    lastMicros = now;
    if (elapsed > 10000) {
        elapsed = 10000;
    }
    owed += elapsed * RATE;
    uint32_t n = owed / 1000000;
    owed %= 1000000;
    /* No faster than the host fetches: this clock and the host's differ. */
    const uint32_t room = (uint32_t)mic.availableForWrite() / 2;
    if (n > room) {
        n = room;
    }
    while (n) {
        int16_t samples[16];
        const uint32_t k = n < 16 ? n : 16;
        for (uint32_t i = 0; i < k; i++) {
            samples[i] = (int16_t)ramp++;
        }
        const size_t took = mic.write(samples, k * 2) / 2;
        if (took != k) {
            refused++;
            ramp = (uint16_t)(ramp - (k - took));   /* what did not fit goes next time */
            break;
        }
        n -= k;
    }
    /* Speaker: count and sum what arrives. */
    uint8_t buf[64];
    size_t got;
    while ((got = speaker.read(buf, sizeof buf)) > 0) {
        speakerBytes += got;
        for (size_t i = 0; i < got; i++) {
            speakerSum += buf[i];
        }
    }
    /* Camera: one frame every 100 ms while the host streams. */
    if (camera.streaming() && !camera.frameInFlight() && (int32_t)(millis() - nextFrameMs) >= 0) {
        nextFrameMs = millis() + 100;
        drawing = frames;
        camera.sendGeneratedFrame(80 * 60 * 2);
    }
    while (SerialDMSeq.available()) {
        const int c = SerialDMSeq.read();
        if (c == '\n') {
            line[used] = 0;
            if (used) {
                reply(line);
            }
            used = 0;
        } else if (c != '\r' && used < sizeof line - 1) {
            line[used++] = (char)c;
        }
    }
}
