# UIAPduino Pro Micro CH32V003 V1.4 HIL

製品ボード用sketchと、ESP32上の`wch-protocols` E132ジグを組み合わせるrelease試験です。
PD1/SWIO、PD7/RESET、USB接続中のPD3/PD4はGPIO/ADC sweepに含めません。SWIOは明示的に
boot entryを要求したときだけ使用し、RESETは使用しません。

1. E132を`esp32-d0wd-v3-0070070d9394`へ書き込む。
2. このdirectoryのsketchを専用FQBN
   `ch32-riscv-ug:ch32v:UIAPDUINO_V003_V14`でbuildし、製品HID bootloaderから書き込む。
3. `uv run tests/manual/uiapduino_fixture/uiapduino_fixture.py --port /dev/ttyUSB0`を実行する。

試験対象はUART、外部header GPIOの入力/出力、A0/A1/A2/A3/A5/A6のLOW/中間/HIGH、I2C
master、SPI mode 0 masterです。A0/A1の中間電位はESP32 DAC、A2/A3/A5/A6はESP32の
pull-upとpull-down同時有効で作ります。後者は抵抗ばらつきがあるため、ADC値は厳密な1/2 scale
ではなく、LOWとHIGHから十分離れていることを判定します。release測定で電圧値を保証する場合は
テストポイントをDMMでも測定してください。

ESP32 GPIO12はESP32自身のVDD_SDIO boot strapなので、PC6/MOSIとの重複配線から外しています。
HILではPC6/MOSIのGPIO2側だけを使用します。
