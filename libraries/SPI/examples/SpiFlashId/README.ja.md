# SpiFlashId

[English](README.md)

通常のsingle-lane SPIモードでcommand `0x9F`を扱える外部Flashから、3 byteのJEDEC IDを読みます。消去・program・write enableのcommandは発行しません。

## 配線と操作

ファームウェア実行中のFlashではなく、別の対応する外部Flashを使います。電源・I/O電圧・pinout・command・追加制御端子の非アクティブ状態を、その部品の仕様書で確認します。SCK・MOSI・MISO・`CS_PIN`（`SS`があればその端子、なければ例として`PA4`）・電源・共通GNDを接続します。必要なHOLD/RESETやWP入力を浮かせないでください。QSPI端子を持つことと、通常SPIモードで動いていることは別です。

基板で使えるSCK/MISO/MOSIの経路と、CS用GPIOを選び、UARTとの競合を避けます。起動時にIDを読み、`i`で再読出しします。メーカー・種別・容量のbyteは、この説明に固定したIDではなく、その部品の資料と照合します。ゼロ埋めせず表示するので、`1`はbyte値`01`です。

## APIと改造時の注意

`SPI.begin(SCK, MISO, MOSI)`で経路を開始できるかを確認します。`SPISettings(1000000, MSBFIRST, SPI_MODE0)`はこの例で使う低めの要求周波数・bit順・modeです。部品に合うか確認してください。`beginTransaction()`はバスを設定しますが、CSは操作しません。スケッチは`0x9F`と3回のdummy転送の間CSをLowに保ち、`endTransaction()`より前に非選択へ戻します。

転送では送信と受信を同時に行います。dummyの送信によって返信を受け取るclockを作ります。SPIにはアドレスACKがありません。`000000`や`FFFFFF`は不自然な値ですが、それ以外の値や開始成功だけで機器が正常だとは判断できません。値がおかしい場合は電源・MISO・CS・配線長・通信手順を確認します。

バスを共有する場合は、ほかの全機器のCSを非選択にし、機器ごとに設定を指定してtransactionを組みます。`beginTransaction()`をスケジューラの排他lockとして扱わず、ISRからバス操作をしないでください。[SPI](../../README.ja.md)と[SPILoopback](../SPILoopback/README.ja.md)も参照してください。

## 実行前の準備

ボードと正確なPart Numberを選び、スケッチの端子を回路図と照合します。ログはハードウェア`Serial`の115200 baud・8N1です。アダプタRXをボードTX、アダプタTXをボードRXへ接続し、GNDを共通にします。I/O電圧を合わせてください。基板のUSB端子だけでこのUARTが見えるとは限りません。

生成された`sketch.yaml`にビルドprofileがあります。ビルド成功は配線や実機動作の確認とは別です。[ビルド規則](../../../../docs/examples-build-rules.ja.md)と[書き込み経路](../../../../docs/upload-routes.ja.md)も参照してください。
