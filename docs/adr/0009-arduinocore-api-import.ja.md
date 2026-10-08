# ADR-0009: ArduinoCore-API 1.5.2 を固定 copy として持つ

- Status: Accepted

## 決定

ArduinoCore-API 1.5.2 の `api/` tree を `cores/arduino/api/` に保持します。upstream commit と各ファイルの
hash は `vendor/arduino-core-api.lock.toml` に記録し、記録された patch 以外は upstream と一致させます。

## 理由

submodule や repository 外への symlink は、GitHub archive、Board Manager、Windows、offline build のいずれかで
追加手順が必要になります。固定 copy なら clone と release archive の両方が自己完結し、upstream との一致も
hash で確認できます。

この directory は LGPL-2.1-or-later であり、project 自身の MIT code とは license 境界を分けます。
upstream 更新は lock と copy を同じ変更で行います。
