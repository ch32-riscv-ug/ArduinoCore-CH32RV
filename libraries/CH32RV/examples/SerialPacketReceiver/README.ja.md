# SerialPacketReceiver

[English](README.md)

状態機械と固定長配列で、小さなバイナリパケットを受信します。UARTの受信単位とパケットの区切りが一致する必要はありません。

## 形式と操作

形式は`A5 長さ payload... checksum`です。長さは1～16 byte。checksumは長さとpayload全byteのXORで、開始の`A5`は含めません。例えば`A5 03 10 20 30 03`が正常です。payload自体に`A5`やゼロも含められます。ここでの数値は16進byteであり、ASCIIの文字列ではありません。

pyserialを導入したPython環境で、ポート名を変更して実行します。

```python
import serial
with serial.Serial('/dev/ttyUSB0', 115200, timeout=2) as port:
    port.write(bytes.fromhex('A5 03 10 20 30 03'))
    port.flush()
    print(port.read(256).decode(errors='replace'))
```

ポートを開くと基板がリセットされる構成では、起動を待ってから送ります。正常なら`Packet bytes: 10 20 30`を表示します。末尾だけ変えてchecksum不一致、`A5 00`や`A5 11`で不正な長さ、`A5 03 10`を送って1秒より長く空けてtimeoutを試します。正常な2パケットをつなげて送った場合は返信も2つになります。

## APIと改造時の注意

`available()`・`read()`で1 byteずつ取り出し、開始・長さ・payload・checksumの状態へ渡します。長さが不正なら配列へ書く前に拒否します。1回のloop()では最大16 byteを処理します。未完成パケットの無通信時間を`millis()`の差で測り、桁あふれをまたいで判定します。

エラー後は後続の`A5`を探しますが、壊れた長さによって次のパケットの一部を取り込むと、すぐには再同期できません。不正な途中パケットの後はtimeoutより長く送信を空けてから再試行します。XORは一部の誤りを見つけるだけで、強いCRCや認証の代わりにはなりません。実用途ではescape、連番、確認応答も検討します。ログ出力は待つ場合があり、連続高速入力はUARTバッファをあふれさせます。これはフレーミングの例であり、速度測定用ではありません。

## 実行前の準備

ボードと正確なPart Numberを選び、スケッチの端子を回路図と照合します。ログはハードウェア`Serial`の115200 baud・8N1です。アダプタRXをボードTX、アダプタTXをボードRXへ接続し、GNDを共通にします。I/O電圧を合わせてください。基板のUSB端子だけでこのUARTが見えるとは限りません。

生成された`sketch.yaml`にビルドprofileがあります。ビルド成功は配線や実機動作の確認とは別です。[ビルド規則](../../../../docs/examples-build-rules.ja.md)と[書き込み経路](../../../../docs/upload-routes.ja.md)も参照してください。
