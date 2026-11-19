# SerialBridge

[English](README.md)

`Serial` と `Serial1` の中継です。片方が受けたバイトを、もう片方へそのまま送ります。PC と、UART で話すモジュール（GPS、Wi-Fi、Bluetooth、別のマイコン）の間に入れて、PC から直接モジュールを操作するのに使います。

## 配線と操作

`Serial`（PC につながる UART か USB）と、`Serial1` の TX・RX をモジュールの RX・TX に交差して、GND を共通にします。I/O の電圧を合わせてください。`Serial1` の端子は variant の既定の経路で、`Serial1.setPins()` で変えられます。PC のターミナルで打った文字がモジュールへ、モジュールの応答が PC に出ます。速さはどちらも `BAUD`（115200）です。

## API と改造時の注意

`available()` で届いた数、`availableForWrite()` で相手の送信バッファの空きを見て、その分だけ `readBytes()` と `write()` で運びます。どちらの向きも待たないので、片方が詰まってももう片方は止まりません。速さが違う組では、遅い側の送信バッファがあふれないよう `setTxBufferSize()` を `begin()` の前に大きくします。

## 実行前の準備

ボードと正確な Part Number を選び、端子を回路図と照合します。生成された `sketch.yaml` にビルドの profile があります。ビルドの成功は、配線や実機の動作の確認とは別です。
