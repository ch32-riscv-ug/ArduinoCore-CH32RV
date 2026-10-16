/* Wiring, operation and expected output: README.md / README.ja.md. */
/* AudioHeadset - ported from EspUsbDevice's example of the same name (renamed
 * EspUsbDevice -> CH32RVUsbDevice, Esp -> CH32RV). Controller support and wiring: see the adjacent README files.
 * docs/usb.ja.md describes the library.
 *
 * A USB Audio Class 1 headset that sends back what the host plays: speaker
 * and microphone, 32 kHz 16-bit mono each (Esp: 48 kHz). A full-speed X035
 * endpoint holds 64 bytes, one millisecond of it, and both streams share the
 * one clock, so they run at the same rate. *
 * requires: flash=48K
 */
#include <CH32RVUsbDevice.h>

CH32RVUsbDevice device;
CH32RVUsbAudioFunction audio(device);
CH32RVUsbAudioPlaybackStream &playback = audio.addPlaybackStream();
CH32RVUsbAudioCaptureStream &capture = audio.addCaptureStream();

void setup()
{
  Serial.begin(115200, SERIAL_8N1, RX, TX);   /* the log: RX, TX are the pads you wired */
  delay(1500);

  playback.addFormat({32000, 1, 2, 16});
  capture.addFormat({32000, 1, 2, 16});

  CH32RVUsbDeviceConfig config;
  config.vid = 0x1209;   /* pid.codes test ID: a product needs its own */
  config.pid = 0x0001;
  config.manufacturer = "ch32-riscv-ug";
  config.product = "CH32RVUsbDevice Headset";
  config.serialNumber = "ch32rv-headset";

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
  const size_t received = playback.read(pcm, sizeof(pcm));
  if (received)
  {
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
