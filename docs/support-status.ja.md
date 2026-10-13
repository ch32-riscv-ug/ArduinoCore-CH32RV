# 系列ごとの実装範囲

全系列を `Generic <SERIES>` として build できます。upload の「未実装」は、同梱の `ch32rv` が
書き込み手段を持たず、`capability-unsupported` で終了する系列です。
「実装済（未検証）」は、ch32rv が同じ family の手順で書き込みますが、その系列では実機で確認していないものです。
経路別、probe 別の対応は [upload-routes.ja.md](upload-routes.ja.md) を参照してください。

| 系列 | build | upload |
|---|---|---|
| CH32V002 | 実装済 | 実装済 |
| CH32V003 | 実装済 | 実装済 |
| CH32V004 | 実装済 | 実装済 |
| CH32V005 | 実装済 | 実装済 |
| CH32V006 | 実装済 | 実装済 |
| CH32V007 | 実装済 | 実装済 |
| CH32M007 | 実装済 | 実装済 |
| CH32V103 | 実装済 | 実装済 |
| CH32V203 | 実装済 | 実装済 |
| CH32V208 | 実装済 | 実装済 |
| CH32V303 | 実装済 | 実装済 |
| CH32V305 | 実装済 | 実装済 |
| CH32V307 | 実装済 | 実装済 |
| CH32V317 | 実装済 | 実装済 |
| CH32X033 | 実装済 | 実装済 |
| CH32X035 | 実装済 | 実装済 |
| CH32L103 | 実装済 | 実装済 |
| CH32M103 | 実装済 | 実装済（未検証） |
| CH32V205 | 実装済 | 実装済 |
| CH32V407 | 実装済 | 未実装 |
| CH32V467 | 実装済 | 未実装 |
| CH32X305 | 実装済 | 実装済（未検証） |
| CH32X315 | 実装済 | 実装済 |
| CH32M030 | 実装済 | 未実装 |

## 製品ボード固有機能

CH32X315 の書込み対象は zero-wait Flash の 192 KiB です。
追加の nonzero-wait 領域を合わせた物理容量を upload.maximum_size に使用しません。
upload の実装と、Arduino の周辺 API・各接続経路の実機検証範囲は別です。

| ボード | 実装範囲 |
|---|---|
| UIAPduino Pro Micro CH32V003 V1.4 | HID bootloader upload。UART / GPIO / ADC / I2C / SPI は Generic CH32V003 の実装を使用 |

ペリフェラル単位の実装範囲は [peripheral-support.ja.md](peripheral-support.ja.md) を参照してください。
