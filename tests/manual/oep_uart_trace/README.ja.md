# oep_uart_trace — X035 USART2 を probe の 2 本目の fixture.uart と突き合わせる（worklist P3 行 2）

`uart_probe` sketch を OEP probe 経由で転送し、console（USART4、GPIO12/6）から命令して USART2（PA2 TX → P4 GPIO48、PA3 RX ← P4 GPIO49）を
probe の 2 本目の `fixture.uart`（`Serial2`、PinTable owner 5）と結ぶ。DUT → P4、P4 → DUT → P4（echo）、長時間連続、受信 overflow、debug reset 後の再開。

```
uv run tests/manual/oep_uart_trace/oep_uart_trace.py [--bauds 9600,115200,460800]
```

## 2026-09-22 の結果（core 4e0cb63 以降、RX ring 64 B 既定）

| 条件 | 結果 |
|---|---|
| DUT → P4 4096 B（LCG）@9600 / 115200 / 460800 | 全 baud で 4096 B 一致 |
| P4 → DUT → P4 echo 2048 B（32 B chunk、echo 待ちで区切る） | 全 baud で 2048 B 一致 |
| overflow: P4 が 512 B を送る間 DUT が 50 ms 読まない | 9600: 231 B（ring は溢れず、peak 39）、115200: 100 B、460800: 63 B（ring 満杯）。**取りこぼしはするが hang せず、その後の echo 256 B は一致** |
| 115200 で 65,536 B 連続 DUT → P4 | 一致、5.2 s（124.6 kbit/s ≈ 理論値の 108 %… command 分を含む実効） |
| debug reset → READY → OPEN → 512 B | 一致 |

最初の run で 9600 の echo が 31/32 で止まる例が 1 回あったが再現せず（単独 / SEND 後 / chunk 16〜64 とも 3 回一致）。
「overflow 後の echo が 9600 で不一致」は試験側の設計不良（P4 の 512 B 送信 533 ms が DUT の RECV 250 ms より長く、残りが次の echo に混入）で、
送信完了を待って排出してから回復 echo をする形に直した。

未実施: 9600 未満、2 Mbps 級、parity / 2 stop bit、USART1/3、`Serial.setRxBufferSize` 相当（core に無い、`CH32_SERIAL_RX_BUFFER_SIZE` は build define）。

## 2026-09-26: OEP v1

OEP v1 に移した（2026-09-26）: 共通の部品は `oep_smoke/trace_kit.py`（v1 の client の上に、この試験が使っていた形を作る）。

x035: 9600 / 115200 / 460800 の DUT → P4、echo、overflow の後の echo、65536 B 連続、reset の後の再開がすべて一致。最初の run で 9600 の echo が 31/32 で止まったが、流し直すと一致（v0 のときと同じ揺れ）。

## 2026-09-29: ボーレートとフォーマットの掃引（`--sweep`）

```
uv run tests/manual/oep_uart_trace/oep_uart_trace.py --sweep --target x035 --run-dir DIR
uv run tests/manual/oep_uart_trace/oep_uart_trace.py --sweep --target v003 --run-dir DIR [--bauds 115200,230400]
```

1 回のビルドのまま、`begin(baud, config)` だけで速さとフォーマットを変える（プロファイルの UART、x035 USART4 PB0/PB1、v003
USART1 PD5/PD6、`-DUUT_SERIAL=Serial<n>`）。F_CPU はスケッチに聞く（`CLOCK`）。振るもの:
- 標準のボーレート 15 通り（9600〜3 Mbps）、8N1。
- core の BRR（`round(F_CPU / baud)`）の丸めが一番ずれる値: BRR 16〜64 のそれぞれで F_CPU / (BRR + 0.49)（BRR 16 で +3.1 %）。
- 範囲外: F_CPU / 12、F_CPU / 8。
- フォーマット 8E1 / 8O1 / 8N2 / 7E1 / 7O2 を、115200 と F_CPU / 16 で。
- `end()` を挟まない `begin()` の切り替え 8 段（速さを上下に、フォーマットも一緒に変える）。
- 送信の途中の `begin()`（`SWITCH`: 16 バイトを書いてすぐ `begin(新しい速さ)`、続けて 16 バイト）。後の塊が新しい速さで欠けずに
  出るか。

1 つの組み合わせごとに:
- tx: DUT → P4 の 256 バイトを 1 バイトずつ比べる。
- rx: P4 → DUT → P4 のエコー 64 バイト（16 バイトずつ詰めて送る）。
- wire: DUT に 16 バイトの塊を 20 ビットの隙間付きで送り続けさせ（`BURST`、キャプチャにトリガーが無いので）、その途中を取って、
  WireSkein で実際のボーレートを測る。期待は `uart(pin, F_CPU / BRR, tol_baud=0.015, max_errors=0, フォーマット)`。BRR 16 未満は
  `uart(pin, None)` で測るだけ。1 ビット 8 サンプル以上（プローブが返した実際のレートで判定）のときだけ。
- P4 / classic ESP32 の fixture.uart は 2 Mbps までなので、それより速いものは wire だけ。

### 結果

| | x035（48 MHz） | v003（24 MHz） |
|---|---|---|
| データ（46 通り） | NG 0 | NG 3（2 Mbps = BRR 12、範囲外） |
| wire | 32 ok / 0 ng | 13 ok / 5 ng（classic ESP32 の sampler の時間軸の乱れ、下） |
| `end()` なしの切り替え 8 段 | すべて一致 | すべて一致 |
| 送信中の `begin()` 3 通り | 後の塊は欠けずに出た | 同じ |

- 送信中の `begin()` では、P4 が新しい速さで受けたバイト数（115200 → 1M で 62 = 16 + 46）から、`begin()` の前に積んだバイトは
  古い速さで出し切ってから切り替わっている（古い速さの 16 バイトが、新しい速さでは雑音の 46 バイトに見える）。
- v003 の wire の NG は、WireSkein が文字の中のエッジと格子のずれを出していて、115200（1 ビット 17 サンプル）でも p99 が 0.47 ビット
  ある。以前の 2 MHz の歩調の遅れを直した後も、sampler がときどき止まる（core 1 の処理で core 0 が止まる、と疑っている）。
  プローブの問題で、試験の期待は緩めていない。

### F_CPU を変えて調べたこと（ビルドし直し。今の試験には無い）

同じ日に、F_CPU（x035 48〜6 MHz、v003 24〜3 MHz）を `build.f_cpu` の上書きで変えて 90 通りずつ流した。
1. **BRR が 16 未満のボーレートを頼むと、USART は F_CPU / 16 で黙って動く。** x035 の範囲外 17 通りは、どれも F_CPU / 16 の
   ±0.13 % で出ていた（6 MHz → 375 kbps、8 MHz → 500 kbps、12 MHz → 750 kbps、16 MHz → 1 Mbps、24 MHz → 1.5 Mbps）。
   `HardwareSerial::begin` は範囲を確かめずに BRR を書く（core は凍結中なので直していない）。
2. **詰めて届くバイトを、1 バイトが CPU 約 200 サイクルより短いと受け落とす。** 送信は正しく、間を空ければエコーも通る。
   x035 の 8 MHz で詰めた 48 バイトを受けると、BRR 32（320 サイクル）で 48、20 で 42、18 で 38、17 で 36、16 で 34。受信の割り込みの
   処理自体は短いので、割り込みの出入り（x035 のスケッチは U モード）かフラッシュの待ちとみている（未確認）。v003 は 24 MHz /
   1.5M（BRR 16）では通り、4 MHz / 250k（BRR 16）では落ちた。
3. 範囲内の送信の実ボーレートは、どの F_CPU でも F_CPU / BRR の +0.05〜+0.18 %（x035）。HSI のずれは F_CPU で少し違う
   （48 MHz で約 +0.07 %、6〜16 MHz で約 +0.14 %）。F_CPU の順を逆にしても同じなので、温まりではない。

途中で直したこと:
- P4 / classic ESP32 の fixture.uart が、範囲外のボーレートに `malformed` を返していた。core §4.3 の `unsupported` にした。
- P4 のキャプチャが、分周の分数の分母が大きいレートで、実際のレートの答えを 32 ビットであふれさせていた（14.886 MHz を頼むと
  1.65 MHz と答え、実際は 14.89 MHz で取っていた）。整数 Hz で答えるようにした。

### 受け落としの修正（2026-09-29）

USART の割り込みハンドラ（`__attribute__((interrupt))`）が `irq()` と `CH32RingBuffer::push()` を関数として呼んでいたため、
割り込みの入口で 16 個のレジスタを退避・復帰していた（32 命令）。`irq()` とリングバッファの `push` / `pop` / `isEmpty` を
強制的に展開し、退避は 6 個になった。x035 を 8 MHz で詰めた 48 バイトを受けた数（前 → 後）: BRR 20（200 サイクル）42 → 48、
18 で 38 → 48、17 で 36 → 47、16 で 34 → 44。詰めたエコーは BRR 17 まで通るようになった（前は BRR 20 で失敗）。BRR 16 ではまだ
少し落ちる（x035 は HPE も有効で、残りはハードウェアの退避と、48 MHz 向けに固定したフラッシュの待ち）。

classic ESP32 の sampler は、区画の flags bit2（時間軸が乱れた、oep-if-capture §2）を立てるようになり、trace_kit は記録の
キャプチャに `time_base_slipped` を付ける。v003 の 21 本は全部に立った（どの窓でも 1 サンプル周期以上止まる）。割り込みを
レベル 5 まで止めると core 1 が固まったので、止めているのは core 1 側の処理とみている。直すにはハードウェアでのサンプル
（I2S の並列取り込み）が要る。
