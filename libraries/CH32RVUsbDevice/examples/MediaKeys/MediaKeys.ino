// en: Setup and usage: README.md. / ja: 配線・使い方: README.ja.md。

#include <CH32RVUsbDevice.h>

CH32RVUsbDevice device;
CH32RVUsbDeviceHidConsumerControl media(device);
CH32RVUsbDeviceHidSystemControl systemControl(device);

static uint32_t lastStepMs = 0;
static uint8_t stepIndex = 0;

struct MediaStep
{
  const char *name;
  uint16_t usage;
};

static const MediaStep MEDIA_STEPS[] = {
    {"volume up", CH32RV_USB_DEVICE_CONSUMER_CONTROL_VOLUME_UP},
    {"volume down", CH32RV_USB_DEVICE_CONSUMER_CONTROL_VOLUME_DOWN},
    {"mute", CH32RV_USB_DEVICE_CONSUMER_CONTROL_MUTE},
    {"play/pause", CH32RV_USB_DEVICE_CONSUMER_CONTROL_PLAY_PAUSE},
    {"next track", CH32RV_USB_DEVICE_CONSUMER_CONTROL_NEXT_TRACK},
    {"previous track", CH32RV_USB_DEVICE_CONSUMER_CONTROL_PREVIOUS_TRACK},
};

static void sendMediaStep()
{
  const MediaStep &step = MEDIA_STEPS[stepIndex % (sizeof(MEDIA_STEPS) / sizeof(MEDIA_STEPS[0]))];
  // en: click() sends both press and release for this media usage. / ja: click()はメディアusageの押下と解放を送る。
  if (media.click(step.usage))
  {
    Serial.printf("MEDIA_KEY %s usage=0x%04x\n", step.name, step.usage);
  }
  else
  {
    Serial.printf("MEDIA_KEY_FAILED %s error=%s\n", step.name, device.lastErrorName());
  }
  stepIndex++;
}

void setup()
{
  Serial.begin(115200, SERIAL_8N1, RX, TX);
  delay(1500);

  CH32RVUsbDeviceConfig config;
  config.vid = 0x1209;
  config.pid = 0x0001;
  config.manufacturer = "ch32-riscv-ug";
  config.product = "CH32RVUsbDevice Media Keys";
  config.serialNumber = "ch32rv-media-keys";

  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }

  Serial.println("USB media keys ready");
}

void loop()
{
  const uint32_t now = millis();
  if (now - lastStepMs >= 3000)
  {
    lastStepMs = now;
    sendMediaStep();
  }

  // en: Power-state actions: enable only for an explicit user action.
  // ja: 電源状態を変える操作は、明示的なユーザー操作に割り当てる場合だけ有効にする。
  // systemControl.click(CH32RV_USB_DEVICE_SYSTEM_CONTROL_STANDBY);
  // systemControl.click(CH32RV_USB_DEVICE_SYSTEM_CONTROL_POWER_OFF);
  // systemControl.click(CH32RV_USB_DEVICE_SYSTEM_CONTROL_WAKE_HOST);

  delay(1);
}
