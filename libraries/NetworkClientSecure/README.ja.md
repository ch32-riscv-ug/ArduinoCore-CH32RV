# NetworkClientSecure

Network ライブラリの `NetworkClient` の上の TLS です。API は ESP32 の Arduino コア 3.x の `NetworkClientSecure` に合わせています。中身は Mbed TLS 4.1(長期サポート版、2029 年 3 月まで)で、選んだ理由は [docs/network-stack.ja.md](../../docs/network-stack.ja.md) にあります。

## 対象と今の状態

- 対象は CH32V307 / V317、V407 / V467、H415〜H417 です。Flash と RAM の量と、乱数の周辺回路(RNG)があることが条件です。ほかの系列ではエラーで止まります。
- 動作は、PC 上で openssl のサーバーを相手に確かめています(`tests/unit/network`)。
  - つながること: ECDSA の鎖と RSA の鎖、根の証明書、バンドル、クライアント証明書。
  - 拒むこと: 違う根、名前の違い、バンドルに無い根、同じ名前の別の鍵の根。
- 実機ではまだ動かしていません(Ethernet のドライバ待ち)。
- **V307 の RAM:** 静的な分が約 30 KB、ハンドシェイク中のヒープが 30〜40 KB で、64 KB に収まらないおそれがあります。V307 は、オプションバイトで RAM を増やせます(Flash と分け合う)が、コアはまだその切り替えに対応していません。

## 使い方

```cpp
#include <NetworkClientSecure.h>

NetworkClientSecure client;

void setup() {
  // ETH などでつないだあと
  configTime(9 * 3600, 0, "pool.ntp.org");   // 証明書の期限を確かめるため(下の「期限」)
  client.setCACert(rootCaPem);               // または setCACertBundle(x509_crt_bundle, x509_crt_bundle_len)
  if (client.connect("example.com", 443)) {
    client.print("GET / HTTP/1.0\r\nHost: example.com\r\n\r\n");
  }
}
```

- `setCACert(pem)`: 根の証明書(PEM)で確かめます。
- `setCACertBundle(bundle, size)`: ESP-IDF の形式のバンドル(根の名前と公開鍵の並び)で確かめます。ESP32CertBundle の `x509_crt_bundle` がそのまま使えます。
- `setInsecure()`: 確かめません。試験の用途だけにしてください。
- `setCertificate(pem)` / `setPrivateKey(pem)`: クライアント証明書を求めるサービス(AWS IoT など)向けです。
- どれも、文字列やバンドルをポインタで持ちます。接続の間は残しておいてください。
- `lastError()` は Mbed TLS の誤りの番号、`verifyFlags()` は証明書の検証の印(`MBEDTLS_X509_BADCERT_...`)です。

## 動きの決まり

- TLS 1.2 です。鍵交換は ECDHE(X25519、P-256、P-384)、証明書は ECDSA と RSA、暗号は AES-GCM と ChaCha20-Poly1305 です。
- **期限:** 証明書の期限は、時計が合ってから(2025 年以降を指してから)確かめます。`configTime()` の SNTP や、RTC から `settimeofday()` で合わせてください。合う前は期限を判断できないので、期限を理由には拒みません。
- **乱数:** 鍵は、チップの RNG から作ります。RNG が誤りを返し続けたときは、弱い乱数で続けず、接続を失敗にします。
- **時間:** ハンドシェイクの公開鍵の計算は、144 MHz で 1〜6 秒かかる見込みです(証明書の種類による)。既定の上限は 120 秒です(`setHandshakeTimeout()`)。待つ間も、ネットワークは回ります。
- V407 / H41x では、楕円曲線の計算を速くする設定にしています(Flash が約 6 KB、ヒープが約 2.5 KB 増え、検証が約 1.5 倍速くなります)。

## 大きさ(2026-10-21、ビルドだけ)

HTTPS の GET をするスケッチ(Network ライブラリ込み)で、V307 は Flash 192 KB、静的な RAM 29.5 KB、H417 は Flash 199 KB でした。TLS の分のヒープは、ハンドシェイク中に 30〜40 KB です。

## ライセンス

Mbed TLS は Apache-2.0 です(`src/mbedtls-src/LICENSE`)。取り込んだ版とファイルのハッシュは `vendor/mbedtls.lock.toml` にあります。
