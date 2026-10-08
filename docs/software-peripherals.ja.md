# ソフトウェア実装ペリフェラル

## 採用基準

hardware peripheral の代わりを GPIO で実装する library は、次を満たす場合だけ同梱します。

- hardware 版と同じ Arduino API の型として利用できる
- core 内部の非公開 API へ依存しない
- 対応速度と CPU 占有を README で明示できる
- silicon 固有 peripheral を持たない小型 package で実用上の価値がある
- timing を保てない条件では黙って通信せず、API で失敗を返せる

## 実装範囲

| library | 実装 | 制約 |
|---|---|---|
| `SoftSPI` | controller | polling 方式。hardware SPI より CPU を占有する |
| `SoftWire` | controller | open-drain GPIO を使用。clock stretching と timeout を扱う |
| SoftSerial | 未実装 | RX の timing、割り込み競合、error reporting の共通仕様がない |
| SoftPWM | 未実装 | timer resource manager と hardware PWM の競合を増やすため独立 API にしない |

`SoftSPI` は `HardwareSPI`、`SoftWire` は `HardwareI2C` を継承し、対応する Arduino API を受け取る library へ
渡せます。個別 API、pin 順、対応 mode は各 library の README を正本とします。

## 実装しないもの

software 実装が hardware の能力を隠したり、割り込みを長時間止めたり、別の同梱 library と resource を奪い合う
場合は core に含めません。外部 library で完結する protocol も、その ecosystem に任せます。
