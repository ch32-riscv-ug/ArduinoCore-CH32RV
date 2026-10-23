# BoardWeActCH32X035

[English](README.md)

WeAct CH32X035 Core Board の端子名を明示して、各周辺機能を組み合わせる出発点です。

## 必要なものと配線

対象基板を用意し、使用する UART・SPI 等をスケッチ冒頭の端子表へ合わせます。USB CDC を使うならデータ接続、PD を試すなら対応電源を使います。

## 操作と期待する結果

LED と PWM が変化し、`Serial` と接続時の `SerialUSB` へ ADC・PD 等の状態が出ます。不要な機能を一つずつ外して用途へ合わせます。

## API と処理の流れ

UART と SPI は端子を渡して初期化します。USB の通信状態と PD の契約は別に読みます。`status(Print &out)` は同じ表示を UART と CDC の両方へ出すための関数です。

## 改造するときの注意

複数機能の端子競合はスケッチのコメントを確認します。I2C のコメントを有効にするときは、それが置き換える USB またはデバッグ端子の用途を外します。別の X035 基板へそのまま流用せず回路図を照合します。

### 基板の接続と端子の兼用

この雛形の対象はCH32X035F8U6搭載のWeAct CH32X035 Core Boardです。手元の基板revisionの回路図も照合します。

| 機能 | スケッチの端子 | 注意 |
|---|---|---|
| LED | PB12 | Highで点灯 |
| `Serial` | TX PA2、RX PA3 | USART2 |
| `Serial1` | TX PB0、RX PB1 | USART4 |
| SPI | SCK PA5、MISO PA6、MOSI PA7、CS PA4 | CSは通常のGPIO |
| ADC | A0 / PA0、A1 / PA1 | 入力電圧をアナログ電源に合わせる |
| PWM | PB11 | UART・SPI・ADCの端子と重ならない配置 |
| USBデータ | D− PC16、D+ PC17 | CC端子とは別 |
| USB PD | CC1 PC14、CC2 PC15 | 契約状態とCDC通信は別に確認 |
| デバッグ | PC18 / PC19 | 任意のI2C経路の一つと兼用 |

任意のI2Cは一組だけ有効にします。SDA PC18 / SCL PC19はデバッグ端子の利用と、SDA PC17 / SCL PC16はUSBデータと排他です。競合する機能を外してから、`<Wire.h>`、選んだ端子の組、`Wire.begin()`のコメントをそろえて解除します。すべてのUSART経路がパッケージに出ているわけではなく、`Serial`をUSART1と決めつけません。

DOWNLOADキーは基板の工場書き込み入口に関わります。通常のアプリ用ボタンとして扱わず、[書き込み経路](../../../../docs/upload-routes.ja.md)に従います。

## 関連資料

- [スケッチ](BoardWeActCH32X035.ino)
- [基本サンプルの準備と一覧](../README.ja.md)
- [スケッチの組み立て方](../../../../docs/sketch-guide.ja.md)
