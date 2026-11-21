/* USB device contracts, media set (tests/instrumented/usb): a UAC1 headset
 * (32 kHz or 16 kHz - one clock, the host picks - 16-bit mono both ways) and
 * a UVC camera (80x60 YUY2, bulk), with
 * the default DFU runtime. Content the host can check exactly:
 *   - microphone: consecutive samples count up by one (wrapping at 16 bits)
 *   - camera: byte i of frame f is (i * 7 + f) & 0xFF
 *   - speaker: what arrives is counted and summed for the host to compare
 *
 *   ready <nonce>   READY <nonce> <build id> <USB started 0/1> <last error>
 *   audio           AUDIO <speaker bytes> <speaker byte sum> <mic writes refused>
 *                   <current rate> <SampleRateChanged events>
 *   video           VIDEO <frames sent> <streaming 0/1> */
#include <Arduino.h>
#include <CH32RV.h>
#include "bench_console.h"
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
static uint32_t rateChanges;
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
        CONSOLE.print("READY ");
        CONSOLE.print(command + 6);
        CONSOLE.print(' ');
        CONSOLE.print(BENCH_BUILD_ID);
        CONSOLE.print(' ');
        CONSOLE.print(USBDevice.started() ? 1 : 0);
        CONSOLE.print(' ');
        CONSOLE.println(USBDevice.lastErrorName());
    } else if (strcmp(command, "audio") == 0) {
        CONSOLE.print("AUDIO ");
        CONSOLE.print((unsigned long)speakerBytes);
        CONSOLE.print(' ');
        CONSOLE.print((unsigned long)speakerSum);
        CONSOLE.print(' ');
        CONSOLE.print((unsigned long)refused);
        CONSOLE.print(' ');
        CONSOLE.print((unsigned long)audio.currentSampleRate());
        CONSOLE.print(' ');
        CONSOLE.println((unsigned long)rateChanges);
    } else if (strcmp(command, "video") == 0) {
        CONSOLE.print("VIDEO ");
        CONSOLE.print((unsigned long)frames);
        CONSOLE.print(' ');
        CONSOLE.println(camera.streaming() ? 1 : 0);
    } else {
        CONSOLE.println("ERROR COMMAND");
    }
}

void setup()
{
    CONSOLE_BEGIN(115200);
    CONSOLE.print("STARTUP ");  // a restart in the middle of a test shows (run_hardware.py)
    CONSOLE.println(CH32RV.resetReasonName());
    speaker.addFormat({RATE, 1, 2, 16});
    speaker.addFormat({RATE / 2, 1, 2, 16});
    mic.addFormat({RATE, 1, 2, 16});
    mic.addFormat({RATE / 2, 1, 2, 16});
    camera.setFormat(CH32RVUsbDeviceVideoFormat::Yuy2);
#ifdef USB_MEDIA_ISOCHRONOUS
    CH32RVUsbVideoConfig video;
    video.transport = CH32RVUsbVideoTransport::Isochronous;
    camera.configure(video);
#endif
    camera.setFrameSize(80, 60);
    camera.setFrameRate(10);
    camera.onFillPayload(fill);
    camera.onFrameComplete(frameDone);
    config.product = "CH32RV usb-media " BENCH_BUILD_ID;
    config.selfPowered = true;
#ifdef USB_TEST_CONTROLLER
    config.controller = USB_TEST_CONTROLLER;   /* run_hardware.py: the controller the record's usb names */
#endif
    USBDevice.begin(config);
    lastMicros = micros();
}

void loop()
{
    deliverEvents();   /* the callbacks set in setup() run here */
    CH32RVUsbAudioEvent event;
    while (audio.pollEvent(event)) {
        if (event.type == CH32RVUsbAudioEventType::SampleRateChanged) {
            rateChanges++;
        }
    }
    /* Microphone: as many samples as real time asks for. */
    const uint32_t now = micros();
    uint32_t elapsed = now - lastMicros;
    lastMicros = now;
    if (elapsed > 10000) {
        elapsed = 10000;
    }
    owed += elapsed * audio.currentSampleRate();
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
    while (CONSOLE.available()) {
        const int c = CONSOLE.read();
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
