# ADR-0004: newlib-nano と GNU++17 を既定にする

- Status: Accepted

## 決定

- C runtime は `--specs=nano.specs` を既定とする
- C++ は GNU++17、exceptions / RTTI / thread-safe statics は無効とする
- `printf` / `scanf` の浮動小数点変換は `rtlib` menu で明示的に追加する
- full newlib は menu に含めない

`rtlib` の値は `nano`、`nanofp`、`nanofs`、`nanofps` です。

## 理由

最小対象には flash 16 KB、RAM 2 KB の型番があります。full newlib や浮動小数点 stdio を既定にすると
基本 sketch でも収まらないため、使用者が必要な変換だけを選ぶ構成にします。GNU++11/14/17 のサイズ差は
実質なく、ArduinoCore-API と一般的な library の互換性を優先して GNU++17 を使います。

`Serial.print(float)` は newlib の `printf` を使わないため、`rtlib` の選択に影響されません。
