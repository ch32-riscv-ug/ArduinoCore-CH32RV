# UIAPduino Pro Micro CH32V003 V1.4 HIL

製品ボード用sketchと、ESP32上の`wch-protocols` E132ジグを組み合わせるrelease試験です。
PD1/SWIO、PD7/RESET、USB接続中のPD3/PD4はGPIO/ADC sweepに含めません。SWIOは明示的に
boot entryを要求したときだけ使用し、RESETは使用しません。

1. E132を`esp32-d0wd-v3-0070070d9394`へ書き込む。
2. このdirectoryのsketchを専用FQBN
   `ch32-riscv-ug:ch32v:UIAPDUINO_V003_V14`でbuildし、製品HID bootloaderから書き込む。
3. `uv run tests/manual/uiapduino_fixture/uiapduino_fixture.py --port
   /run/board-identify/by-id/esp32-d0wd-v3-0070070d9394`を実行する。

ESP32のdevice名は再列挙で変わるため、`/dev/ttyUSB0`のような番号固定名を試験手順には
使用しません。

試験対象はUART、外部header GPIOの入力/出力、A0/A1/A2/A3/A5/A6のLOW/中間/HIGH、I2C
master、SPI mode 0 masterです。A0/A1の中間電位はESP32 DAC、A2/A3/A5/A6はESP32の
pull-upとpull-down同時有効で作ります。後者は抵抗ばらつきがあるため、ADC値は厳密な1/2 scale
ではなく、LOWとHIGHから十分離れていることを判定します。release測定で電圧値を保証する場合は
テストポイントをDMMでも測定してください。

ESP32 GPIO12はESP32自身のVDD_SDIO boot strapなので、PC6/MOSIとの重複配線から外しています。
HILではPC6/MOSIのGPIO2側だけを使用します。

## 2026-09-19 実機結果

専用FQBNの13,780 byte imageを製品HID `1209:b803`から書き込み、次を1回のsuiteで確認しました。

- UART 115200 bps
- 外部headerの12信号すべてのdigital input/output
- ADC: A0 `0/398/908`、A1 `0/392/908`、A2 `0/352/909`、A3 `0/347/908`、
  A5 `0/350/906`、A6 `0/350/906`（LOW/中間/HIGH、10 bit値）
- I2C masterからESP32 slave `0x42`への`1234`、slaveからmasterへの`DEADBEEF`
- SPI mode 0 full duplex: master送信`31425364`、slave応答`C35A6996`

A5/PD5とA6/PD6はUART TX/RXと兼用です。試験sketchはUARTを止め、一度の`ADCSWEEP`で
3状態を時刻同期して測定してからUARTを戻します。特にRX兼用のA6は、ADC中に次のcommandを
受信する方式では試験できません。
