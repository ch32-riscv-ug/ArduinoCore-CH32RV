// en: Turn a potentiometer and button into USB MIDI control and note messages.
// ja: 可変抵抗とボタンの入力をUSB MIDIの制御値と音符に変える。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

#include <CH32RVUsbDevice.h>

// en: The device groups USB settings; the objects declared below add functions for the host.
// ja: deviceがUSB全体をまとめ、下のオブジェクト宣言でホストに見せる機能を追加する。
CH32RVUsbDevice device;
CH32RVUsbDeviceMidi MIDI(device);

static constexpr uint8_t MIDI_CHANNEL = 0;
static constexpr uint8_t MIDI_NOTE_C4 = 60;
static constexpr uint8_t MIDI_CC_CUTOFF = 74;
static constexpr int CONTROLLER_PIN = A0;
static constexpr int BUTTON_PIN = PA1;

static uint16_t smoothedAdc = 0;
// en: 255 is outside the MIDI control range, so the first valid reading is always considered changed.
// ja: MIDI制御値の範囲外の255で始め、最初の有効な値は必ず変化ありと判断する。
static uint8_t lastCcValue = 255;
static bool buttonPressed = false;

static uint8_t readControllerValue()
{
  const uint16_t value = analogRead(CONTROLLER_PIN);
  // en: Mix mostly the old reading with a little new input to reduce small ADC fluctuations.
  // ja: 前回値を多めに、新しい値を少し混ぜ、ADCの小さな揺れを抑える。
  smoothedAdc = static_cast<uint16_t>((static_cast<uint32_t>(smoothedAdc) * 15 + value) / 16);
  // en: This mapping assumes 12-bit input; match analogReadResolution() or adjust the range.
  // ja: この変換は12 bit入力を仮定。読み取り分解能または入力範囲を合わせる。
  return static_cast<uint8_t>(map(smoothedAdc, 0, 4095, 0, 127));
}

static void updateController()
{
  const uint8_t ccValue = readControllerValue();
  // en: Send only changed values; this prevents repeated control messages while the knob is still.
  // ja: 値が変わったときだけ送り、つまみが止まっている間の同じ制御値の連送を避ける。
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

  // en: Both button edges matter: release must send Note Off.
  // ja: 押下と解放の両方を扱い、解放時にはNote Offを送る。
  const bool ok = pressed ? MIDI.noteOn(MIDI_CHANNEL, MIDI_NOTE_C4, 96)
                          : MIDI.noteOff(MIDI_CHANNEL, MIDI_NOTE_C4, 0);
  Serial.printf("MIDI_BUTTON %s %u\n", pressed ? "ON" : "OFF", ok ? 1 : 0);
}

void setup()
{
  // en: This Serial is hardware UART for diagnostics, separate from the USB connection being demonstrated.
  // ja: このSerialはログ用のハードウェアUART。ここで試すUSB通信の接続とは別。
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

  // en: Start all configured functions together; success starts the stack, while the host configures it afterward.
  // ja: 設定した機能をまとめて開始する。成功は内部の開始を示し、ホストの認識はその後に進む。
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
