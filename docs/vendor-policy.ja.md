# 第三者ソースの取込方針

## 原則

- 通常ビルド中に第三者 source を download しない
- 取り込む directory / file を allowlist で限定する
- upstream URL、tag または commit、license、各 file の hash を lock file に記録する
- upstream copy と project 自身の code の license 境界を明示する
- 記録されていないローカル変更を検証ツールで拒否する
- upstream 全体や WCH EVT 全体を便宜的に copy しない

## 固定対象

| lock | 対象 | repository 内の実体 |
|---|---|---|
| `vendor/arduino-core-api.lock.toml` | ArduinoCore-API | `cores/arduino/api/` |
| `vendor/tinyusb.lock.toml` | TinyUSB | `libraries/TinyUSB/src/` |
| `vendor/ch32-device-data.lock.toml` | ch32-device-data の consumer surface | 生成物のみ。上流 table は copy しない |

lock file が version と hash の正本です。この文書には変更のたびに動く値を複製しません。

## Patch

upstream copy へ patch が必要な場合は、lock file に対象 file、理由、patch を記録します。
無記録の直接編集は認めません。patch は project 固有の glue で代替できず、upstream へそのまま反映できない場合に
限定します。

## License

root の MIT License は第三者成果物を再 license しません。配布物には各成果物の license / notice を含め、
source 提供等の追加条件がある binary を再配布する場合は、その条件を満たす配布経路を用意します。

利用条件を確認できない file は取り込みません。本方針は法的助言ではなく、provenance と license condition を
review 可能に保つための project rule です。
