# サンプルスケッチ案内

[English](examples.md)

目的に合う小さな例から始め、配線と入出力を確認してから組み合わせるための案内です。
各スケッチの隣の `README.ja.md` に、必要なもの・操作・期待する結果・API の使い方・改造時の注意を記載しています。

## 目的から選ぶ

| 用途 | ライブラリ・案内 | 最初に試す例 |
|---|---|---|
| 基本の入出力 | [CH32RV](../libraries/CH32RV/examples/README.ja.md) | PinCapabilities → Blink / SerialEcho → Timing |
| ハードウェア I2C | [Wire](../libraries/Wire/examples/I2CScanner/README.ja.md) | I2CScanner |
| GPIO による I2C | [SoftWire](../libraries/SoftWire/examples/SoftI2CScanner/README.ja.md) | SoftI2CScanner |
| ハードウェア SPI | [SPI](../libraries/SPI/examples/SPILoopback/README.ja.md) | SPILoopback |
| GPIO による SPI | [SoftSPI](../libraries/SoftSPI/examples/SoftSPILoopback/README.ja.md) | SoftSPILoopback |
| サーボ | [Servo](../libraries/Servo/examples/Sweep/README.ja.md) | Sweep |
| ISR とのデータ受け渡し | [CH32RVSyncKit](../libraries/CH32RVSyncKit/examples/README.ja.md) | QueueFromInterrupt / DeadlinePeriod |
| 複数の仕事を組み合わせる | [CH32RVTaskKit](../libraries/CH32RVTaskKit/examples/README.ja.md) | BasicWork → ButtonBlinkConsole |
| PC につなぐ USB 機器 | [CH32RVUsbDevice](../libraries/CH32RVUsbDevice/examples/README.ja.md) | SerialUSBEcho → 目的のクラス |
| ボードへ USB 機器をつなぐ | [CH32RVUsbHost](../libraries/CH32RVUsbHost/examples/README.ja.md) | DeviceInfo → 目的のクラス |
| USB-C 電源の交渉 | [CH32RVUsbPd](../libraries/CH32RVUsbPd/examples/README.ja.md) | ListProfiles / CycleVoltages |
| 高速 ADC | [CH32RVHsAdc](../libraries/CH32RVHsAdc/README.ja.md) | HsAdcRead → HsAdcStream |
| GPIO の取り込み | [CH32RVLogicCapture](../libraries/CH32RVLogicCapture/README.ja.md) | LogicBurst → LogicStream |
| デバッグ接続の文字出力 | [SerialSDI](../libraries/SerialSDI/README.ja.md) | HelloSDI / PrintfToSDI |
| デバッグ接続の双方向端末 | [SerialDMDATA](../libraries/SerialDMDATA/README.ja.md) | HelloDMDATA |
| デバッグ接続の双方向端末 | [SerialDMSeq](../libraries/SerialDMSeq/README.ja.md) | HelloDMSeq |
| RAM バッファ経由の端末 | [SerialRTT](../libraries/SerialRTT/README.ja.md) | HelloRTT / RttEcho |

USB を使う前に、Device・Host・PD の役割を分けて考えます。
Device は PC 等につなぐ機器、Host は接続された機器を制御する側、PD は電源の交渉です。
UART ログを見るための USB-UART アダプタも、MCU 自身の USB データ接続とは別です。

## サンプルを動かす手順

1. 対象の `.ino` と個別 README を読み、正確な型番、電源、入力電圧、使う端子を確認します。
2. ボードと Part Number を選び、スケッチ冒頭の端子定数を自分の基板へ合わせます。
3. [書き込み経路](upload-routes.ja.md) に合う方法で書き込みます。
4. ログや操作先のアプリを開き、README の操作と期待する結果を照合します。
5. 一つずつ変更し、失敗時の戻り値・切断・入力なしの場合も確認します。

IDE では「ファイル → スケッチ例 → ライブラリ名」から開きます。
CLI でビルドする場合は、各共通ガイドのコマンドのボード指定とスケッチのパスを置き換えます。
profile はビルド条件であり実機動作の保証ではありません。詳細は [examples のビルド規則](examples-build-rules.ja.md) を参照します。

## 組み合わせる前の確認

| 確認するもの | 理由 |
|---|---|
| 端子・電源・周辺回路 | 同じ pad の兼用、I/O 電圧、モーター等の電源条件はソフトウェアだけでは解決しない |
| API の単位 | ms / µs、Hz、mV / mA、サンプル数 / バイト数を区別する |
| 戻り値 | 成功の `bool`、実際に処理した長さ、未受信の `-1` を区別する |
| オブジェクトと配列の寿命 | function、callback の参照先、DMA や直接転送のバッファを使用中に破棄・上書きしない |
| 処理を実行する場所 | ISR、`loop()`、通知 callback、応答を作る provider を区別する |
| 待ちと再試行 | 例の `delay()` や大量ログが、ほかの入力・転送の処理を遅らせることがある |

[スケッチの組み立て方](sketch-guide.ja.md) では、時刻比較、割り込みとの受け渡し、
`deliverEvents()` と短い仕事の組み合わせを説明しています。
対応系列や実装上の上限は、個別例に複製せず、各ライブラリ README と
[USB の対応表](usb.ja.md#対応状況)・[ペリフェラル対応](peripheral-support.ja.md) を参照してください。
