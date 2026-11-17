# LowVoltageDetect

[English](README.md)

プログラマブル電圧検出（PVD）: 電源が閾値を下回ったらコールバックを呼びます。PVDのEXTIの線を使います。必要なのはUART接続だけで、実際に下げるには可変電源を使います。PVDの割込みの無いチップ（H41x、M030、V004 / V007 / M007）では、その旨を表示します。

## 操作

1. ターミナルを開いてボードをリセットします。PVDの段、3000 mVに対して選んだ閾値、電源の電圧（`getVddMillivolts()`）を表示します。
2. 電源を閾値より下げると`Low voltage #1 (still low)`と表示します。電源がすでに下にあるとき（段がすべて3000 mVより下の系列では最も高い段を選ぶ）は、すぐに表示します。
3. `t`を送ると、PVDの線をソフトウェアから起こし（`triggerExtiLine()`）、本当に下がったときと同じ道でコールバックが呼ばれます。

## APIと改造時の注意

`CH32RV.setLowVoltageDetect(mv, callback)`は、`mv`以上で最も低い閾値（`mv`がすべてより上なら最も高いもの）を選び、そのmVを返します。コールバックはVDDが閾値を下回るたびにPVDの割込みの中で呼ばれ、設定したときにすでに下にあれば、すぐに1回呼ばれます。リセットではなく知らせです。ESP32のブラウンアウト検出と違ってチップは動き続け、スケッチは状態を保存できます。`isLowVoltage()`は今のPVDの出力を読みます。閾値の近くでは何度か下回ることがあります。段はデータシートの代表値で、系列ごとに数段です（`lowVoltageLevel(i)`）。

ピンでないほかのEXTIの線も同じ道で受けられます: `CH32RV.attachExtiLine(CH32RV_EXTI_USBFS_WAKEUP, callback)`など。`enableExtiLineWakeup()`で、その線で休止から起きます。名前は系列にその線があるときだけ定義されます（`#ifdef CH32RV_EXTI_PVD`）。

## 実行前の準備

ボードと正確なPart Numberを選びます。ログはハードウェア`Serial`の115200 baud・8N1です。アダプタRXをボードTX、アダプタTXをボードRXへ接続し、GNDを共通にします。I/O電圧を合わせてください。電源を下げるときは、チップの動作範囲の中にとどめてください。

生成された`sketch.yaml`にビルドprofileがあります。ビルド成功は配線や実機動作の確認とは別です。[ビルド規則](../../../../docs/examples-build-rules.ja.md)と[書き込み経路](../../../../docs/upload-routes.ja.md)も参照してください。
