# ButtonDebounce

[English](README.md)

機械式スイッチの接点が短時間揺れても、押下と解放を一度ずつ扱います。USBボタン例へ進むための、ポーリングによる基本例です。

## 配線と操作

通常は開いている押しボタンスイッチを、`BUTTON_PIN`（初期値`PA1`）とGNDの間につなぎます。`INPUT_PULLUP`により離すとHigh、押すとLowです。別系統の電源へスイッチをつながないでください。PA1をほかの用途で使う基板では、空いている入力へ変更します。

UART端末を開き、押す・押したままにする・離すを試します。安定した新しい押下で`Pressed, count: ...`を一度表示し、押し続けても回数は増えません。離すと`Released`を表示します。起動時から押されていた場合は初期状態として扱い、新しい押下には数えません。一度離してから押すと数え始めます。

## APIと処理

`digitalRead()`で電気的な状態を読み、最新の生値を`candidateLevel`、その変化時刻を`changedAtMs`へ保存します。25 msの間変わらなかった場合だけ`stableLevel`へ反映します。別の`reportedLevel`と比較して変化を取り出すため、押したままの状態を何度も押したと解釈しません。

`millis()`の差で時間を測り、カウンタの桁あふれもまたげます。チャタリング待ちの`delay()`はなく、毎回ほかの短い処理へ戻れます。人が押すスイッチ向けのソフトウェア処理であり、パルスカウンタではありません。短い入力や読み取りの間の変化は見逃す場合があります。loop()を短く保ち、待ち時間は実際のスイッチに合わせます。`pressCount`はRAM上の値なのでリセットで0に戻ります。

[ButtonKeyboard](../../../CH32RVUsbDevice/examples/ButtonKeyboard/README.ja.md)・[ButtonMouse](../../../CH32RVUsbDevice/examples/ButtonMouse/README.ja.md)では、この状態をホストの入力へ変換します。

## ボードとUARTの準備

ボードと正確なPart Numberを選び、各端子を回路図と照合します。Genericの別名はvariantの定義であり、基板コネクタの表示と同じとは限りません。UART・書き込み／debug・ほかの周辺回路との端子競合を避けます。生成された`sketch.yaml`のprofileはビルド条件であり、配線の確認結果ではありません。

ログはハードウェア`Serial`の115200 baud・8N1です。アダプタRX←ボードTX、アダプタTX→ボードRX、共通GNDで接続し、I/O電圧を合わせます。このUARTはUSB HIDとは別で、基板のUSB端子から自動的に見えるわけではありません。

[ビルド規則](../../../../docs/examples-build-rules.ja.md)・[書き込み経路](../../../../docs/upload-routes.ja.md)・[サンプル一覧](../../../../docs/examples.ja.md)も参照してください。
