# ADR-0003: Startup、vector、linker はこの project が所有する

- Status: Accepted

## 決定

- reset 処理は共通の `cores/arduino/crt0_ch32.S` に置く
- series 差は生成された vector include、IRQ 定義、clock 定義で与える
- memory layout は型番ごとの生成 linker script で与える
- vector table は flash 先頭に置き、entry 0 を reset jump とする
- WCH EVT の startup source は取り込まない

## 理由

EVT の startup を系列ごとに copy すると、修正が分散し、vector の欠落や CRT 動作の不一致が起きます。
共通 reset 処理とデータ由来の差分に分けると、constructor、data/bss 初期化、heap、vector 配置を一つの
契約として保守できます。また、EVT の directory 構造と再配布条件に platform が依存しません。

vector table を flash 先頭に固定するのは、QingKe V2 が `mtvec` の下位 bit を保持できず、途中に置いた
table を正しく参照できないためです。全系列を同じ配置にすると世代別の例外も不要になります。
