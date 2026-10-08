# Architecture Decision Records

ADR には、現行実装を拘束する決定と、その決定を変更するときに必要な理由だけを残します。
検討過程、承認履歴、実験ログは Git 履歴で参照します。

すべての掲載 ADR は **Accepted** です。実装されていない案や将来候補は ADR に置きません。

| ADR | 決定 |
|---|---|
| [0001](0001-device-data-repository.ja.md) | device data の正本を独立 repository に置く |
| [0002](0002-toolchain-distribution.ja.md) | compiler は xPack GCC を上流 release から取得する |
| [0003](0003-owned-startup-vector-linker.ja.md) | startup / vector / linker をこの project が所有する |
| [0004](0004-runtime-and-cxx.ja.md) | newlib-nano と GNU++17 を既定にする |
| [0005](0005-board-structure-and-fqbn.ja.md) | Generic board は series 単位、型番は menu で選ぶ |
| [0006](0006-rtos-policy.ja.md) | core は bare metal とし RTOS を同梱しない |
| [0007](0007-user-build-option-injection.ja.md) | `build.extra_flags` を利用者向けに予約する |
| [0008](0008-upload-strategy.ja.md) | upload / discovery / monitor は `ch32rv` に一本化する |
| [0009](0009-arduinocore-api-import.ja.md) | ArduinoCore-API 1.5.2 を固定 copy として持つ |
| [0010](0010-pin-numbering.ja.md) | pad 名を安定した sparse pin 番号で表す |
| [0012](0012-usb-stack.ja.md) | USB stack には TinyUSB を使う |
| [0013](0013-bundled-libraries.ja.md) | 同梱 library の範囲と examples の置き場所を限定する |

番号 0011 と 0014 は現行 platform が依存しない tool 配布案だったため、文書から除外しました。
番号は再利用しません。
