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
| `SoftSPI` | controller、peripheral（`SPI_PERIPHERAL`） | polling 方式。hardware SPI より CPU を占有する。peripheral は SCK を polling で追う |
| `SoftSPISlave` | slave（CS で区切る） | 端子変化の割込み。1 ビット 1 回、縁から半周期のうちに処理する必要がある。SCK と CS は別の EXTI の線 |
| `SoftWire` | controller、target | open-drain GPIO を使用。clock stretching と timeout を扱う。target は端子変化の割込みで追い、処理の間 SCL を握る。SDA と SCL は別の EXTI の線 |
| `SoftwareSerial` | 送受信（Arduino の SoftwareSerial の API、`begin(speed, config)` で 5〜8 ビット・パリティ・ストップ 2） | 受信はスタートビットの端子変化の割込みでフレームを読む（SysTick で各ビットの中央、割込みの遅れは `listen()` で測って差し引く）。送信は 1 フレームの間割込みを止める。AVR と同じ半二重、listen できるのは 1 つ。RX は別のビット番号の EXTI と共存。2026-11-17、ベンチ未確認 |
| SoftPWM | 未実装 | timer resource manager と hardware PWM の競合を増やすため独立 API にしない |

`SoftSPI` は `SPIClass`（SPI ライブラリ）、`SoftWire` は `TwoWire`（Wire ライブラリ）、`SoftwareSerial`（SoftwareSerial ライブラリ）は `Stream` を継承し、対応する Arduino API を受け取る library へ
渡せます。個別 API、pin 順、対応 mode は各 library の README を正本とします。

## 実装しないもの

software 実装が hardware の能力を隠したり、割り込みを長時間止めたり、別の同梱 library と resource を奪い合う
場合は core に含めません。外部 library で完結する protocol も、その ecosystem に任せます。
