# I2CRegisterRead

[English](README.md)

レジスタ番号を送ってから一定長を読み、書き込み結果と受信長の両方を確認します。[I2CSlaveRegister](../I2CSlaveRegister/README.ja.md)を動かす2台目のCH32と組にすれば、特定のセンサモジュールなしで試せます。

## 配線と操作

2台のSDA同士、SCL同士、GNDを接続します。SDAとSCLには互換性のあるI/O電源への外部pull-upを用意し、基板に実装済みのものも考慮します。独立に給電した2台の電源出力同士は接続しません。I2Cの経路が診断UARTと同じpadを使わないよう確認し、必要なら別の有効な経路へ変更します。

片方にslave例、もう片方にこのmaster例を書き込みます。毎秒、アドレス`0x42`のレジスタ0から3 byteを読みます。16進数で`32`、変化する受信書き込み回数、書き換え可能レジスタの値（初期値0）が並びます。回数は255の次に0へ戻り、レジスタ選択も含めてSTOPで受け取った書き込みを数えます。

## APIと改造時の注意

`begin(SDA, SCL)`は不正・使用中のピン経路で失敗します。`beginTransmission()`・`write()`でレジスタ選択を準備し、`endTransmission()`で送信して結果を得ます。`requestFrom()`の戻り値は実際の受信長です。短い応答を完全なレジスタとして解釈せず、この例では残りを読み捨てて次の周期に再試行します。

対のslaveでは`STOP_AFTER_REGISTER = true`を使います。このコアの`onReceive()`はSTOPで通知されるためです。外部機器がrepeated STARTを要求する場合は`false`にします。`endTransmission(false)`でバスを保持し、続く`requestFrom()`で次のSTARTを発行して、読み取りの終わりにSTOPを出します。アドレス・レジスタ番号の幅・byte順・長さは、その機器の仕様に合わせてください。この3 byteの配置はサンプル独自のもので、センサ共通の形式ではありません。

NACKなら相手不在、アドレス違い、相手の電源を確認します。timeoutならpull-upと信号レベルを確認し、[I2CTimeoutRecovery](../I2CTimeoutRecovery/README.ja.md)と[Wire API](../../README.ja.md)を参照します。アドレスは7 bit表記であり、read/write bitを付けてシフトした値ではありません。

## 実行前の準備

ボードと正確なPart Numberを選び、スケッチの端子を回路図と照合します。ログはハードウェア`Serial`の115200 baud・8N1です。アダプタRXをボードTX、アダプタTXをボードRXへ接続し、GNDを共通にします。I/O電圧を合わせてください。基板のUSB端子だけでこのUARTが見えるとは限りません。

生成された`sketch.yaml`にビルドprofileがあります。ビルド成功は配線や実機動作の確認とは別です。[ビルド規則](../../../../docs/examples-build-rules.ja.md)と[書き込み経路](../../../../docs/upload-routes.ja.md)も参照してください。
