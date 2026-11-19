# EcdcAes

[English](README.md)

H41x の暗号器（ECDC）で AES-128 を使います。FIPS-197 の例のブロックを暗号化して復号し、次に文字列を CTR で暗号化して戻します。

## 必要なものと配線

ECDC のある石（H415 / H416 / H417）。配線は要りません。

## 操作と期待する結果

`Serial` に次のように出ます。1 行目は FIPS-197 の C.1 の値と同じです。

```
ECB encrypt: 69C4E0D86A7B0430D8CDB78070B4C55A
ECB decrypt: 00112233445566778899AABBCCDDEEFF
CTR encrypt: ...
CTR decrypt: Hello from the ECDC
```

## API と処理の流れ

`begin()` が鍵を覚え、`encrypt()` / `decrypt()` が ECB で、`ctr()` が CTR で変換します。変換はハードが行います。
