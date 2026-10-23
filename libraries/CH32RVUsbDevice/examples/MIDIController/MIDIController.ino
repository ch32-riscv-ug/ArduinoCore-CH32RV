// en: Setup and usage: README.md. / ja: 配線・使い方: README.ja.md。

#include <CH32RVUsbDevice.h>

CH32RVUsbDevice device;
CH32RVUsbDeviceMidi MIDI(device);

static constexpr uint8_t MIDI_CHANNEL = 0;
static constexpr uint8_t MIDI_NOTE_C4 = 60;
static constexpr uint8_t MIDI_CC_CUTOFF = 74;
static constexpr int CONTROLLER_PIN = A0;
static constexpr int BUTTON_PIN = PA1;

static uint16_t smoothedAdc = 0;
static uint8_t lastCcValue = 255;
static bool buttonPressed = false;

static uint8_t readControllerValue()
{
  const uint16_t value = analogRead(CONTROLLER_PIN);
  smoothedAdc = static_cast<uint16_t>((static_cast<uint32_t>(smoothedAdc) * 15 + value) / 16);
  // en: This mapping assumes 12-bit input; match analogReadResolution() or adjust the range.
  // ja: この変換は12 bit入力を仮定。読み取り分解能または入力範囲を合わせる。
  return static_cast<uint8_t>(map(smoothedAdc, 0, 4095, 0, 127));
}

static void updateController()
{
  const uint8_t ccValue = readControllerValue();
  if (ccValue == lastCcValue)
  {
    return;
  }
  lastCcValue = ccValue;

  if (MIDI.controlChange(MIDI_CHANNEL, MIDI_CC_CUTOFF, ccValue))
  {
    Serial.printf("MIDI_CC control=%u value=%u\n", MIDI_CC_CUTOFF, ccValue);
  }
}

static void updateButton()
{
  const bool pressed = digitalRead(BUTTON_PIN) == LOW;
  if (pressed == buttonPressed)
  {
    return;
  }
  buttonPressed = pressed;

  // en: Both button edges matter: release must send Note Off. / ja: 押下と解放の両方を扱い、解放時にはNote Offを送る。
  const bool ok = pressed ? MIDI.noteOn(MIDI_CHANNEL, MIDI_NOTE_C4, 96)
                          : MIDI.noteOff(MIDI_CHANNEL, MIDI_NOTE_C4, 0);
  Serial.printf("MIDI_BUTTON %s %u\n", pressed ? "ON" : "OFF", ok ? 1 : 0);
}

void setup()
{
  Serial.begin(115200, SERIAL_8N1, RX, TX);
  delay(1500);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  smoothedAdc = analogRead(CONTROLLER_PIN);

  CH32RVUsbDeviceConfig config;
  config.vid = 0x1209;
  config.pid = 0x0001;
  config.manufacturer = "ch32-riscv-ug";
  config.product = "CH32RVUsbDevice MIDI Controller";
  config.serialNumber = "ch32rv-midi-controller";

  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }

  Serial.println("USB MIDI controller ready");
}

void loop()
{
  updateController();
  updateButton();
  delay(5);
}
