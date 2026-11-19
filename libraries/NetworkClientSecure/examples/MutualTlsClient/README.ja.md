# MutualTlsClient

[English](README.md)

クライアント証明書の TLS（相互認証）です。ボードはサーバーを自分の CA で確かめ、サーバーはボード自身の証明書を確かめます。
手元のサーバー（[mtls_server.py](mtls_server.py)、`openssl s_server -Verify 1 -www` でもよい）と話し、ハンドシェイクが通ったか、TLS の版、サーバーの最初の返事の行、
または失敗の理由を表示します。

## 必要なものと配線

- Ethernet の MAC と乱数の回路（RNG）を持つボード: CH32V307 / V317、V407 / V467、H415〜H417。
- ボードの RJ45 を、DHCP があってインターネットへ出られる LAN（時計を合わせる SNTP のため）につなぐ。同じ LAN に PC。
- シリアルモニタは `Serial`、115200 baud。
- **CH32V307 / V317:** ツール → Flash / RAM Split → 192 KB + 128 KB（ハンドシェイクに 30〜40 KB のヒープが要る）。
  Stack Check を Reset on overflow にするなら Stack Size を 8 KB に（クライアント証明書のハンドシェイクはスタックを約 4.2 KB 使う）。

## 証明書とサーバーの用意（PC）

試験用の CA、サーバー（PC の IP を名前に入れる）、ボードの証明書を openssl で作ります（例は PC が 192.168.1.10）。

```sh
openssl ecparam -name prime256v1 -genkey -noout -out ca.key
openssl req -x509 -new -key ca.key -sha256 -days 3650 -subj "/CN=test CA" -out ca.pem
openssl ecparam -name prime256v1 -genkey -noout -out server.key
openssl req -new -key server.key -subj "/CN=192.168.1.10" -out server.csr
printf "subjectAltName=IP:192.168.1.10,DNS:192.168.1.10\nextendedKeyUsage=serverAuth\n" > server.ext
openssl x509 -req -in server.csr -CA ca.pem -CAkey ca.key -CAcreateserial -days 825 -extfile server.ext -out server.pem
openssl ecparam -name prime256v1 -genkey -noout -out board.key
openssl req -new -key board.key -subj "/CN=board 1" -out board.csr
printf "extendedKeyUsage=clientAuth\n" > board.ext
openssl x509 -req -in board.csr -CA ca.pem -CAkey ca.key -CAcreateserial -days 825 -extfile board.ext -out board.pem
openssl pkcs8 -topk8 -nocrypt -in board.key -out board8.key
python3 mtls_server.py 192.168.1.10
```

[certs.h](certs.h) の `SERVER_HOST` を PC の IP に、`CA_CERT` を ca.pem、`CLIENT_CERT` を board.pem、`CLIENT_KEY` を
board8.key の中身にします（1 行ずつ `"...\n"` の形）。

## 操作と期待する結果

```text
Address 192.168.1.81
mutual TLS 0x304 in 1286 ms
server says: HTTP/1.0 200 ok
```

PC の側には `TLSv1.3 client {'commonName': 'board 1'}` が出ます。`CLIENT_CERT` を別の CA の証明書にすると、サーバーが
断ります（PC に `refused: ... certificate verify failed`）。TLS 1.3 ではボードの側のハンドシェイクが先に終わるので、ボードには
`mutual TLS 0x304` のあと `server says: (nothing)`（返事の無いまま閉じる）と出ます。どちらも CH32V307 で 2026-11-20 に確かめました。

## API と処理の流れ

| API | この例での使い方 |
|---|---|
| `setCACert(CA_CERT)` | サーバーの証明書を確かめる CA。 |
| `setCertificate(CLIENT_CERT)` / `setPrivateKey(CLIENT_KEY)` | ボードの証明書と鍵（PEM、鍵は暗号化しない）。サーバーに求められたら見せ、鍵で署名する。 |
| `connect(host, port)` / `lastError()` / `verifyFlags()` | 接続と、失敗したときの Mbed TLS の誤りと検証の印。 |

3 つともポインタで持たれるので、接続の間は残しておきます。鍵はフラッシュに平文で入るので、取り出されて困る鍵には
読み出し保護（ch32rv の `target option`）を考えてください。

