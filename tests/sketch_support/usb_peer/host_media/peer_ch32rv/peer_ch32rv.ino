/* Another CH32RV board as the USB audio device for tests/peer/usb/host/media: a UAC1 headset
 * (CH32RVUsbAudioFunction), 48000 Hz, mono, 16-bit, both ways. The DUT (CH32RVUsbHost) is the host; this board runs
 * CH32RVUsbDevice on the controller the bench record names (build_opt.h: PEER_USB_CONTROLLER, written by
 * run_hardware.py).
 *
 * Capture (to the host) carries a counter, one 16-bit sample after another; playback (from the host) is checked for
 * the host's counter the same way. The commands are peer_s3's, on the debug probe's console (SerialDMSeq); answers
 * start "CD ":
 *   ?                    CD READY <configured 0/1>
 *   audstat              CD AUDRX <playback bytes> <breaks in the host's counter> <capture underruns>
 *   audreset             CD AUDRESET
 *   audctl               CD AUDCTL <muted 0/1> <volume, 1/256 dB> <mute / volume changes the host made> */
#include <Arduino.h>
#include <SerialDMSeq.h>
#include <CH32RVUsbDevice.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifndef PEER_USB_CONTROLLER
#define PEER_USB_CONTROLLER CH32RVUsbController::Auto
#endif

CH32RVUsbDevice device;
CH32RVUsbAudioFunction audio(device);
CH32RVUsbAudioPlaybackStream &playback = audio.addPlaybackStream();
CH32RVUsbAudioCaptureStream &capture = audio.addCaptureStream();

static int16_t captureNext;
static uint32_t rxBytes, rxBreaks, controlChanges;
static int16_t rxExpect, volume;
static bool rxSynced, muted;
static char line[96];
static size_t used;

static void say(const char *format, ...)
{
    char text[160];
    va_list ap;
    va_start(ap, format);
    vsnprintf(text, sizeof text, format, ap);
    va_end(ap);
    SerialDMSeq.print(text);
}

static void run(char *cmd)
{
    char *arg = strchr(cmd, ' ');
    if (arg) *arg++ = '\0';
    if (!strcmp(cmd, "?")) say("CD READY %u\n", device.ready() ? 1 : 0);
    else if (!strcmp(cmd, "audstat"))
        say("CD AUDRX %lu %lu %lu\n", (unsigned long)rxBytes, (unsigned long)rxBreaks,
            (unsigned long)capture.stats().underrunCount);
    else if (!strcmp(cmd, "audreset")) {
        rxBytes = rxBreaks = 0;
        rxSynced = false;
        say("CD AUDRESET\n");
    } else if (!strcmp(cmd, "audctl")) say("CD AUDCTL %u %d %lu\n", muted ? 1 : 0, volume, (unsigned long)controlChanges);
    else say("CD UNKNOWN %s\n", cmd);
}

void setup()
{
    SerialDMSeq.begin(115200);
    playback.addFormat({48000, 1, 2, 16});
    capture.addFormat({48000, 1, 2, 16});
    CH32RVUsbDeviceConfig config;
    config.manufacturer = "ch32rv-tests";
    config.product = "CH32RV peer media";
    config.serialNumber = "CH32RVPEERMEDIA";
    config.dfuRuntime = false;
    config.controller = PEER_USB_CONTROLLER;
    const bool began = device.begin(config);
    say("CD BOOTED %u %s\n", began ? 1 : 0, device.lastErrorName());
}

void loop()
{
    deliverEvents();
    int16_t block[24];
    while (capture.availableForWrite() >= sizeof block) {
        for (int16_t &s : block) s = captureNext++;
        capture.write(block, sizeof block);
    }
    int16_t in[48];
    size_t n;
    while ((n = playback.read(in, sizeof in)) != 0) {
        rxBytes += n;
        for (size_t i = 0; i < n / 2; i++) {
            if (rxSynced && in[i] != rxExpect) rxBreaks++;
            rxExpect = (int16_t)(in[i] + 1);
            rxSynced = true;
        }
    }
    CH32RVUsbAudioEvent event;
    while (audio.pollEvent(event)) {
        if (event.type == CH32RVUsbAudioEventType::StreamStateChanged &&
            event.target == CH32RVUsbAudioEventTarget::Playback && event.enabled)
            rxSynced = false;
        if (event.type == CH32RVUsbAudioEventType::MuteChanged) {
            muted = event.muted;
            controlChanges++;
        }
        if (event.type == CH32RVUsbAudioEventType::VolumeChanged) {
            volume = event.volumeDb256;
            controlChanges++;
        }
    }
    while (SerialDMSeq.available()) {
        const int c = SerialDMSeq.read();
        if (c == '\r') continue;
        if (c == '\n') {
            line[used] = '\0';
            if (used) run(line);
            used = 0;
        } else if (used + 1 < sizeof line) {
            line[used++] = (char)c;
        }
    }
}
