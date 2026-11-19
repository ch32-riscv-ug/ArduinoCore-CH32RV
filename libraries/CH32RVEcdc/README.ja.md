# CH32RVEcdc

[English](README.md)

CH32H415 / H416 / H417 の暗号器（ECDC、CH32H417RM の 41 章）を使います。AES（鍵 128 / 192 / 256 bit）と SM4（128 bit）を、
ECB と CTR でハードが変換します。バイトの順は規格のとおりで、FIPS-197 や GB/T 32907 のテストベクトルがそのまま通ります。

```cpp
#include <CH32RVEcdc.h>
CH32RVEcdc ecdc;
ecdc.begin(CH32RVEcdc::AES, key, 16);     // 16・24・32 バイト（SM4 は 16）
ecdc.encrypt(plain, cipher, 64);          // ECB。長さは 16 の倍数
ecdc.decrypt(cipher, plain, 64);
ecdc.ctr(iv, plain, cipher, 100);         // CTR。長さは自由。同じ呼び出しで復号
```

## API

| 関数 | 内容 |
|---|---|
| `begin(algorithm, key, keyLen)` | 鍵を覚えて暗号器のクロックを入れる。AES は 16・24・32 バイト、SM4 は 16 バイト。それ以外と ECDC の無い系列は false |
| `end()` | クロックを切り、覚えた鍵を消す |
| `encrypt(in, out, len)` / `decrypt(in, out, len)` | ECB。`len` は 16 の倍数（それ以外は false） |
| `ctr(iv, in, out, len)` | CTR。`iv` は 16 バイトのカウンタで、ブロックごとに big-endian で 1 ずつ進む（NIST SP 800-38A の例と同じ）。`iv` は書き換えない |

`in` と `out` は同じバッファでもかまいません。

## 速さと置き場所

32 バイト境界にあるバッファ（`alignas(32)`）の 2 ブロックずつは、暗号器が RAM を自分で読み書きします（CPU はその間待つ）。
それ以外のバッファ、奇数個目のブロック、CTR の 16 バイトに満たない端は、レジスタ経由で 1 ブロックずつ変換します。
結果は同じで、CPU の時間が多くかかるだけです。

これは実機で分かったハードの性質によります（CH32H417、2026-11-19）。
- RAM どうしの変換は、書き先の番地の bit 4 を無視します（32 バイト境界から 16 バイトずれた番地に渡すと、16 バイト手前に書く）。
- 32 バイト単位で動きます（3 ブロックを渡すと 4 ブロック書き、4 つ目は 3 つ目の写し）。
- 1 ブロックずつの CTR はカウンタを鍵の展開のときに一度だけ取り込み、進めません。そのためこの経路の CTR は、カウンタを ECB で暗号化して CPU で XOR します。

## 試験

`selfapi::test_ecdc`（tests/instrumented/selfapi）が、FIPS-197 の C.1〜C.3（AES-128 / 192 / 256）と GB/T 32907 の A.1（SM4）の
暗号化と復号、3 ブロックの ECB、NIST SP 800-38A の F.5.1（AES-128 CTR）を、32 バイト境界・1 バイトずれ・16 バイトずれ・
50 バイト・同じバッファで確かめます。H417 で通っています（2026-11-19）。
