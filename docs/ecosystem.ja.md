# 外部エコシステム調査

調査基準日: 2026-08-17

外部プロジェクトの状態は変化します。採用前には、固定するtag/commitとライセンスを再確認してください。

## ArduinoCore-API

- Repository: [arduino/ArduinoCore-API](https://github.com/arduino/ArduinoCore-API)
- hardware非依存Arduino APIを定義し、host-based unit testを持つ
- 第三者coreにも利用が推奨されている
- C++11以上を要求する
- License: LGPL-2.1

本プロジェクトでは、固定revisionを原則無改変で取り込み、CH32依存部分だけを実装する案が有力です。小容量デバイスでのcode/RAM costは実測が必要です。

## WCH公式Arduino core

- Repository: [openwch/arduino_core_ch32](https://github.com/openwch/arduino_core_ch32)
- Board Manager index: [package_ch32v_index.json](https://github.com/openwch/board_manager_files/blob/main/package_ch32v_index.json)
- 調査時点のBoard Manager最新platformは1.0.4
- packageはGCC 8.2.0、OpenOCD 1.0.0を使用

挙動比較、pin mapping、互換性調査の重要な資料ですが、full SPL取込、古いtoolchain、公開APIとvendor実装の結合を引き継ぐため、fork元にはしない案です。

## WCH EVT

family別の公式repositoryがあります。

- [openwch/ch32v003](https://github.com/openwch/ch32v003)
- [openwch/ch32v20x](https://github.com/openwch/ch32v20x)
- [openwch/ch32v307](https://github.com/openwch/ch32v307)
- [openwch organization](https://github.com/openwch)

旧コアの変更可能な配布URLより追跡しやすい一方、familyを横断する統一versionはありません。ファイルにはWCH製MCUでの使用を条件とする通知が含まれることがあり、通常のOSSライセンスがrepository全体に明示されていない場合があります。

EVT全体の再配布は前提にせず、利用条件を確認したファイルだけを由来付きで扱います。

## ch32fun

- Repository: [cnlohr/ch32fun](https://github.com/cnlohr/ch32fun)
- License: MIT
- 2026年も活発に更新されている
- generic RISC-V GCC、低レベルstartup/register実装、minichlink、豊富なexampleを持つ

SoC差分、code size、register操作、programmerの重要な参照実装です。一方、Arduino APIでもEVT compatibility layerでもないため、本プロジェクトの内部APIとして直接依存する案ではありません。

## ch32-rs ecosystem

- [ch32-rs/ch32-rs](https://github.com/ch32-rs/ch32-rs)
- [ch32-rs/ch32-data](https://github.com/ch32-rs/ch32-data)
- [ch32-rs/wlink](https://github.com/ch32-rs/wlink)
- [ch32-rs/wchisp](https://github.com/ch32-rs/wchisp)

`ch32-data`はdevice DB生成の入力候補ですが、各値を最新reference manualと照合する必要があります。`wlink`はWCH-Link protocolと対応chipの有用な資料ですが、README自身がproduction readyではないとしています。`wchisp`はUSB/UART ISP用で、WCH-Link backendではありません。

## probe-rs

- Repository: [probe-rs/probe-rs](https://github.com/probe-rs/probe-rs)
- Candidate release: [v0.32.0](https://github.com/probe-rs/probe-rs/releases/tag/v0.32.0)
- License: MIT OR Apache-2.0

v0.32.0ではCH32 target定義生成と、WCH-Linkの`AttachChip`応答によるCH32自動判定が追加されています。flash、verify、debug/GDB/DAPを統一できる可能性があり、新しいupload backendの第一候補です。

これは「採用決定」ではありません。対象SKU、memory構成、WCH-Link firmware、各host OSでの認定が必要です。

## WCH OpenOCD

- Repository: [openwch/openocd_wch](https://github.com/openwch/openocd_wch)

公式Arduino coreで利用されていますが、調査時点のGitHub repositoryは再現可能なsource/build手順を得にくい構成です。互換fallback候補とし、再配布する場合は対応source、patch、build手順、GPL通知を確認します。

## WCH-Link解析資料

- [RINS WCH-Link documentation](https://perigoso.github.io/rins/wch-link/index.html)
- [wlink protocol notes](https://github.com/ch32-rs/wlink/blob/main/protocol.md)
- [WCH-Link User Manual](https://www.wch-ic.com/downloads/WCH-LinkUserManual_PDF.html)

RINSはfirmware 2.5で、USB serialが全個体同じ`0001A0000000`だったと記録しています。新しいfirmwareでも同じかは未検証です。いずれにしても、serialや列挙indexだけへ依存しないfixture設計が必要です。

WCH-LinkのUSB host protocolは、公開された公式仕様を確認できず、上記の解析資料はfirmware依存です。一方、ターゲット側の一線式debugについては、WCHが[QingKe V2 Debug Manual](https://github.com/openwch/ch32v003/blob/main/RISC-V%20QingKeV2%20Microprocessor%20Debug%20Manual.pdf)と[CH32F103を使ったCH32V003書き込み例](https://github.com/openwch/ch32v003/tree/main/CH32V003_1Line_Base_on_CH32F103)を公開しています。これはV003/QingKe V2の一次資料であり、他のQingKe世代へ無条件に一般化しません。

## Programmer関連ライセンス

| Project | 調査時点のライセンス | 注意 |
|---|---|---|
| probe-rs | MIT OR Apache-2.0 | 固定versionと第三者依存も記録する |
| wlink | MIT OR Apache-2.0 | production readyではない |
| minichlink/ch32fun | MIT | 利用するsource範囲を固定する |
| wchisp | GPL-2.0 | USB/UART ISP用。配布形態を確認する |
| WCH OpenOCD | GPL系 | 対応source、patch、build手順を保持する |

## ESP32等を利用したprogrammer

- [ESP32-S2 CH32V003 programmer](https://github.com/cnlohr/esp32s2-cookbook/tree/master/ch32v003programmer)
- [experimental RVSWDIO programmer](https://github.com/cnlohr/rv003usb/tree/master/rvswdio_programmer)

実現可能性の資料にはなりますが、汎用ESP32-S3 programmerが安定した依存先として存在するとは確認できませんでした。最初のリリースをcustom programmer完成へ依存させない方針が妥当です。

## Arduino platform仕様

- [Platform specification](https://docs.arduino.cc/arduino-cli/platform-specification/)
- [Pluggable Discovery specification](https://docs.arduino.cc/arduino-cli/pluggable-discovery-specification/)
- [Package index specification](https://docs.arduino.cc/arduino-cli/package_index_json-specification/)

Pluggable Discoveryは独自protocolのportを列挙し、`address`とpropertiesをupload recipeへ渡せます。固定fixture laneをArduino IDE/CLIへ公開するために利用できます。

## テスト参考実装

- [tanakamasayuki/host-arduino-core](https://github.com/tanakamasayuki/host-arduino-core)
- [tanakamasayuki/I2CDeviceDB](https://github.com/tanakamasayuki/I2CDeviceDB)

前者からはsketch単位のpytest構成、host execution、複数profile、artifact保存を、後者からはsigrok制御、`READY → arm → RUN → DONE`、raw capture保存、replayを参照します。
