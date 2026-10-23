# I2CTimeoutRecovery

[English](README.md)

アドレスのNACKとバスのtimeoutを区別し、明示的な操作でだけ信号線の復旧を試します。構成を把握したバスで使い、応答する相手には[I2CSlaveRegister](../I2CSlaveRegister/README.ja.md)も利用できます。

## 配線と操作

I/O電圧、共通GND、外部SDA/SCL pull-upを確認します。SDA/SCLとUARTが同じpadを使わないようにします。最初は正常なバスで試します。

- `p`で`0x42`をprobeします。対のslaveがいれば結果は0、timeout flagはclearになります。
- 信号線が正しくpull-upされ、そのアドレスの相手だけがいない場合は、timeoutではなくアドレスNACK（code 2）になります。
- すでに信号線がLowに保持されたバスでは、probeがtimeout（code 5）とflagのsetになる場合があります。異常を作るために出力端子を電源へ短絡させないでください。
- 全機器に対して復旧操作を行ってよい場合だけ`r`を送ります。両信号線が解放されたかを表示します。その後`p`で再確認します。線の解放は、相手の存在や内部状態の正常を保証しません。

## APIと改造時の注意

`setWireTimeout()`の単位はusです。`getWireTimeoutFlag()`は消すまで保持されるので、調べたい操作の前にclearし、操作後に読みます。NACKとtimeoutでは確認箇所が違います。timeoutではpull-up不足や、相手によるSDAの保持も考えます。

`clearBus()`は復旧用clockとSTOPを発行し、接続された全機器へ届きます。短絡、給電の誤り、信号線を保持し続ける機器は直せません。この例は復旧を自動的に繰り返しません。timeoutはドライバ内の個々の待ちの上限であり、transaction全体が厳密にその時間内に終わる意味ではありません。エラーコードと復旧の意味は[Wire](../../README.ja.md)を参照してください。

## 実行前の準備

ボードと正確なPart Numberを選び、スケッチの端子を回路図と照合します。ログはハードウェア`Serial`の115200 baud・8N1です。アダプタRXをボードTX、アダプタTXをボードRXへ接続し、GNDを共通にします。I/O電圧を合わせてください。基板のUSB端子だけでこのUARTが見えるとは限りません。

生成された`sketch.yaml`にビルドprofileがあります。ビルド成功は配線や実機動作の確認とは別です。[ビルド規則](../../../../docs/examples-build-rules.ja.md)と[書き込み経路](../../../../docs/upload-routes.ja.md)も参照してください。
