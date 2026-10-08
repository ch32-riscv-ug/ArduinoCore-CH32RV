# ADR-0013: 同梱 library と examples の範囲を限定する

- Status: Accepted

## 決定

同梱する library は次のいずれかに限定します。

1. Arduino で事実上標準の API を提供する
2. CH32 固有の core 機能を利用者へ公開する
3. timer 等の hardware resource を core と調停する必要がある

一般的な sensor、display、filesystem driver は同梱しません。

Arduino IDE は library 配下の examples を列挙するため、core API の examples は `libraries/CH32RV/examples/` に置きます。
各同梱 library は README、examples、`keywords.txt` を持ち、利用条件を自身で説明します。

## 理由

便利さだけを基準に同梱すると release size と保守対象が増え続けます。hardware と platform に固有の責務だけを
同梱し、汎用 driver は Arduino Library Manager の ecosystem に任せることで、core の更新と無関係な依存を避けます。
