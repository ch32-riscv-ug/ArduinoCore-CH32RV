# I2CSlaveRegister

[English](README.md)

[I2CRegisterRead](../I2CRegisterRead/README.ja.md)等のmasterへ、3 byteのレジスタを公開します。callbackは短い受け渡しだけを行い、診断表示はメイン処理へ分けます。

## 配線とレジスタ

master例の2台構成の配線を使い、外部pull-upとI/O電圧も確認します。slaveアドレスは7 bit表記の`0x42`です。

| レジスタ | 読み取り | 書き込み |
| --- | --- | --- |
| 0 | 識別値`0x32` | 無視 |
| 1 | STOPで受け取った書き込み回数（256で周回） | 無視 |
| 2 | ユーザー値、初期値0 | 1 byteを保存 |

書き込みの先頭byteがレジスタ番号です。続くbyteを順番に書き、読み取り専用・範囲外の番号は無視します。読み取りは選択番号から末尾までを返し、別のtransactionへ進んでも選択番号を自動更新しません。masterは先にSTOP付きの書き込みでレジスタを選んでください。例えば`02 7B`をSTOP付きで書くと、レジスタ2へ`0x7B`を保存し、次の読み取りでその値を返します。全体を読む場合は0を選び直します。範囲外や、準備した応答より多く読み取った部分は`FF`を返します。

masterからレジスタ2へ書く場合は、次のtransactionの結果を確認します。

```cpp
Wire.beginTransmission(0x42);
Wire.write((uint8_t)2);
Wire.write((uint8_t)0x7B);
const uint8_t error = Wire.endTransmission();
```

## 操作とAPI

slaveを起動してからmasterを動かします。slave側は毎秒、受信回数とユーザー値を表示します。`Wire.begin(address, SDA, SCL, frequency)`でslaveを開始し、成否を確認します。バスのclockを出すのはmasterです。`onReceive(count)`でSTOP時に書き込みを受け取り、`onRequest()`から`Wire.write()`して返信を準備します。

両callbackは割り込み内で動きます。`Serial`、待機、動的確保、別のバスtransactionの開始は行いません。返信用の小さなローカル配列は、`Wire.write()`が戻る前にWireの返信バッファへコピーするため使えます。`loop()`は関連する共有値を短い割り込み禁止区間でコピーし、割り込みを戻してから表示します。この区間は通常のメイン処理からだけ使います。

対の例では意図的にSTOPを使います。このコアのslaveではrepeated STARTの読み取り前にレジスタ選択のcallbackが届かないため、その手順を検証する相手には使わないでください。メッセージはWireの設定容量内に収めます。callback・timeout・制約は[Wire](../../README.ja.md)も参照してください。

## 実行前の準備

ボードと正確なPart Numberを選び、スケッチの端子を回路図と照合します。ログはハードウェア`Serial`の115200 baud・8N1です。アダプタRXをボードTX、アダプタTXをボードRXへ接続し、GNDを共通にします。I/O電圧を合わせてください。基板のUSB端子だけでこのUARTが見えるとは限りません。

生成された`sketch.yaml`にビルドprofileがあります。ビルド成功は配線や実機動作の確認とは別です。[ビルド規則](../../../../docs/examples-build-rules.ja.md)と[書き込み経路](../../../../docs/upload-routes.ja.md)も参照してください。
