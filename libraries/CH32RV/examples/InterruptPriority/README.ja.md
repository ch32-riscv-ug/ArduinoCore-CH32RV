# InterruptPriority

[English](README.md)

周辺の割り込みの優先度を設定し、遅い割り込み処理にコアの割り込み（ここでは `millis()` を進める SysTick）が割り込めるようにします。

## 必要なものと配線

UART モニタを用意します。

## 操作と期待する結果

0.5 秒ごとにタイマーの割り込み処理が数ミリ秒空回りし、その間に `millis()` が進んだ量を表示します。`level 0`（コアと同じレベル。SysTick は待つので `0 ms`）の 4 行と、`level 1`（1 つ下。SysTick が処理に割り込むので数 ms）の 4 行が交互に出ます。入れ子が効かない石では、後の 4 行も 0 のままです。

## API と処理の流れ

`ch32rv_priority.h` が PFIC の優先度のバイトを設定します: `ch32rv_irq_set_priority(irqn, priority)`。小さいほど急ぎです。`CH32RV_IRQ_PRIORITY(level)` は割り込みのレベル（0 から `CH32RV_IRQ_PREEMPT_LEVELS - 1`。多くの系列で 2、V30x と M030 で 4、H41x の V5F で 8）のバイトです。タイマーの割り込みの番号は capability の `update_irq`、端子のものは `ch32rv_pin_irqn(pin)`、ほかは `CH32RV_IRQN_<name>` です。コアは自分の割り込みも含めてすべて 0（一番急ぎ）のままにするので、下げた割り込み処理にはコアの割り込みが割り込みます。

## 改造するときの注意

ほかの割り込みに割り込まれる処理から、その割り込みのドライバーを呼びません（たとえば USART の割り込みを下げたあと、それに割り込める処理から `Serial` を呼ぶ）。全体の約束は CH32RV ライブラリの README にあります。

## 関連資料

- [スケッチ](InterruptPriority.ino)
- [基本サンプルの準備と一覧](../README.ja.md)
- [サンプルの案内](../../../../docs/examples.ja.md)
- [スケッチの組み立て方](../../../../docs/sketch-guide.ja.md)
