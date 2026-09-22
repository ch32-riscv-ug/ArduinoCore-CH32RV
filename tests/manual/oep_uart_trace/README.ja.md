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
