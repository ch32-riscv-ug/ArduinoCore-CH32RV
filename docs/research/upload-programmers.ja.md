# R-17: 書き込み経路・書き込み器・書き込みソフトの事前調査

調査基準日: 2026-08-19
関連: [Q-040〜Q-049](../open-questions.ja.md)、[upload-and-fixture](../upload-and-fixture.ja.md)(frontend設計とprobe選択はそちらが正本)、[ADR-0005](../adr/0005-board-structure-and-fqbn.ja.md)(メニュー構成)

## 調査目的

「defaultの書き込み方式を何にし、何をオプションでサポートするか」を決める材料を揃える。対象は (a)WCH-Link系debug IF、(b)チップ内蔵の工場bootloader(USB/UART ISP)、(c)ソフトUSB等の独自bootloader、(d)互換書き込み器のエコシステム、(e)書き込みソフトの選択。UIAPduino Pro Micro CH32V003のようなboard固有方式のサポートも視野に入れる。

## 確認済み事実1: family×書き込み経路の対応表

一次資料(各DS0/RM、wchisp chip DB、EVT実物コード)で確認。

| family | debug IF | USB ISP | UART ISP | 工場BLへのSWエントリ | BOOTの入り方 |
|---|---|---|---|---|---|
| V003 | **SDI 1-wire** | ✕(USBなし) | ○(PD5/PD6固定、BL 1920B) | **○**(`SystemReset_StartMode`) | BOOT0ピンなし。OB+SWリセット(ブランクチップは不可) |
| V00X | **SDI 1-wire** | ✕ | ○(BL 3328B) | **○** | 同上。BOOT領域のユーザー領域化可(DS0明記) |
| V103 | SWD 2-wire | ○ | ○ USART1 | ✕ | BOOT0/BOOT1ピン |
| V203/V208 | SWD 2-wire | ○ | ○ USART1(BL 28KB) | ✕ | BOOT0/BOOT1ピンのみ |
| V205 | SWD 2-wire | ○ | ○ USART2(BL 3328B) | ✕ | BOOT0/BOOT1(小パッケージは内部VSS固定=ISP不可、DS0注記) |
| V30x/V317 | SWD 2-wire | ○ | ○ USART1 | ✕ | BOOT0/BOOT1(小PKG注意) |
| V407 | SWD 2-wire | ○ | ○ USART1 | ✕ | BOOT0/BOOT1(物理ピン配置は要確認) |
| X033/X035 | SWD 2-wire | ○ | ○ | **○**(実物確認: `SystemReset_StartMode()`) | BOOT0ピンなし。電源投入時PC17(USB DP)プルアップ or SWエントリ。BOOT領域ユーザー化可 |
| X315 | SWD 2-wire | ○ USBFS | ○(keyless download) | **○** | OB START_MD(出荷時BOOT起動がデフォルト) |
| L103 | SWD 2-wire | ○ | ○ USART2 | ✕ | BOOT0/BOOT1ピン |
| M030 | **1-wire/2-wire両対応** | **✕(工場BL自体がない)** | ✕ | − | −(EVTに独自IAP例のみ) |
| H417 | SWD 2-wire | ○ USBFS | ○ | ○ | BOOT0ピンなし(entry条件はRM要精読) |

**構造的な発見**: 自動書き込み(ボタンなし)の可否は「工場BLへのSWエントリ」の有無で割れる。
- **SWエントリ可**: V003/V00X/X03x/X315/H417 → アプリ協力(1200bps touch等)で無操作書き込みが可能
- **BOOT0ピンのみ**: V103/V2xx/V3xx/V407/L103 → 工場BLへの自動entryは不可(独自BLなら可)
- **M030特例**: ISPなし。WCH-Link(または独自IAP)一択

## 確認済み事実2: 書き込み器ハードウェアのエコシステム

| programmer | 方式 | 対応 | 状態 |
|---|---|---|---|
| **WCH-LinkE** | SDI 1-wire+SWD+UART付き | 全CH32 RISC-V。$4〜7で入手容易 | 純正。事実上の標準 |
| WCH-LinkW | 同上+無線 | LinkE同等 | 純正 |
| WCH-Link(無印) | 2-wire系のみ | **V003/V00X系は書けない**(V103/V20x/V30x等のみ) | 純正旧世代 |
| ESP32-S2 funprog | SWIO bitbang(HID) | minichlink標準backend | cnlohr系、実績あり |
| Ardulink | Arduino UNO等をSWIO書き込み器化 | minichlink標準backend | 〃 |
| NHC-Link042 | STM32F042ベース | minichlink標準backend | 〃 |
| **rvswdio programmer** | **CH32V003自身を書き込み器化**(HID、minichlink互換) | V003/00x/20x/30x/X03x等(V103/CH58xは非対応)。README自身が実験的と明記 | UIAPduinoの公式復旧手段 |
| rv003usbブートローダ | ターゲット自身のソフトUSB HID BL(1920B) | V003。minichlinkのbackend(`pgm-b003fun`) | UIAPduino出荷搭載 |
| picorvd | RP2040 PIOでSWIO、GDB server | V003。uploader統合なし("very alpha") | 実験的 |
| esp32s3-ch32-programmer(ご提示) | ESP32-S3独自実装+独自Python CLI | V003。minichlink非互換。2026-07開始・MIT | ごく初期 |
| Flipper Zero wch_swio_flasher | minichlink互換エミュ | V003 | 初期段階 |

**UIAPduino Pro Micro CH32V003**(スイッチサイエンス扱い、オープンソース): rv003usbベースのカスタムBLを出荷搭載し、USB Type-C 1本+リセット操作で書き込み。コアはopenwchベース、独自のboard manager index運用。復旧手段はLinkE / rvswdio / スタンドアロンflasherの3種を公式案内。

## 確認済み事実3: 書き込みソフト

| tool | 対応範囲 | license/配布 | 評価 |
|---|---|---|---|
| **probe-rs 0.32.0** | V003/V00X/V1/V2/V3/X03x等。**V407/X315/M030/V205は未対応**(targets実物確認) | MIT/Apache、GH Releases単一バイナリ | ◎ chip自動判別あり。既定方針([upload-and-fixture](../upload-and-fixture.ja.md))の第一候補のまま。coverage gapが判明 |
| wlink 0.1.2 | ほぼ全family(H417含む、V317要テスト) | MIT/Apache、GH Releases | ○ ただし自称"not production ready" |
| minichlink | ch32fun対応全般。**probe 6種+工場ISP BLまで扱える** | MIT。整理されたRelease無し | ○ 互換probe対応の要。Arduinoコア採用例あり(arduino-wch32v003) |
| wchisp | USB ISP(V103/V20x/V30x/V317/X03x/L103/CH5xx)。V003/V00XはUARTのみ(nightly) | **GPL-2.0**、GH Releases | ○ ISP経路の本命。openwch coreがtool定義済み(ただし手動配置方式) |
| WCHISPTool(_CMD) | 全WCH(公式) | proprietary、再配布条件不明 | △ 参照用 |
| WCH OpenOCD | 全CH32(公式Arduinoコアが使用) | GPL系だが**ソース非公開のbin置き場・2023年から停滞** | △ probe-rs gapの暫定fallback |

前例: openwch coreのupload_methodは「WCH-SWD(OpenOCD、default)/WCH-ISP(wchisp、**バイナリ手動配置・BOOT手動**)」の2択。STM32duinoはwrapperスクリプト1本にSWD/Serial/DFU等を集約。Arduino platform仕様の`upload.use_1200bps_touch`/`wait_for_upload_port`は仕様標準(Leonardo/RP2040系で実績)。X035では**CDC 1200bps touch→`SystemReset_StartMode()`で自動書き込みの実証例あり**(maxgerhardt)。V20xには**tinyuf2ポート(UF2 bootloader)が実在**。

EVTのIAP例(全familyに独自BL雛形あり: UART/USB両対応+フラッシュ分割+ジャンプ処理)は独自bootloader開発の参考実装になるが、ホストがWindows専用exeのため、採用時はプロトコル流用+クロスプラットフォームCLI(またはUF2/CDC化)の自作が前提。

## 推奨(提案): default/オプション構成

**default: WCH-LinkE(debug IF経由)**。理由: 全family共通、ブランクチップOK、verify/reset完結、$4-7で入手容易、fixture/HILと同一経路。

- backend: probe-rs第一候補は維持。ただし**V407/X315/M030/V205のcoverage gap**が判明したため、当面はfamily別にbackendを出し分けるハイブリッド(gap分はwlink/OpenOCD、またはprobe-rsへのtarget追加貢献)。この差異は[upload-and-fixture](../upload-and-fixture.ja.md)のfrontend(`ch32-upload`案)で吸収し、boards.txtのメニューには経路名だけを見せる

**オプション(メニュー`upload_method`、family別に出し分け)**:

| メニュー項目 | 対象family | 内容 |
|---|---|---|
| WCH-Link(default) | 全family | probe-rs系frontend。互換probe(minichlink系6種)は「未認定だが動作報告歓迎」のTier 2扱い |
| USB-ISP | V103/V20x/V30x/V317/X03x/L103 | wchisp。**X03x/X315/H417はCDC 1200bps touch+SWエントリで自動書き込み化**、BOOT0ピン系は「手動BOOT+リセット」注記付き |
| UART-ISP | V003/V00X | 工場UART BL経由。SWエントリ協力が要るため優先度低(提供はする) |
| board固有BL | named board単位 | UIAPduino(rv003usb HID)等。ADR-0005のpnum項目に`upload.tool`を紐付けて提供。V20x系はtinyuf2(UF2)も候補 |

**独自bootloaderの位置付け**(自作OKとの方針を受けて):
- 汎用チップ向けにはまず工場BL+SWエントリを活用(V003/V00X/X03x/X315/H417は追加開発なしで自動書き込み可能性あり)
- 独自BLは「boardプロダクト向け付加価値」(UIAPduino型)として設計する。候補: V003=rv003usb(実績あり)、V00X=UART BL(BOOT領域3328Bのユーザー化がDS0で公式に許可されている)、USB family=UF2/CDC系
- 書き込みソフトの自作(`ch32-upload` frontend)は既定方針どおり。backend gapとprobe選択問題(Q-041)が既存toolで埋まらない場合に本体開発へ昇格

## 追記(2026-08-19): backendのprobe選択能力をsourceで確認

「複数のWCH-LinkEから対象を一意に選ぶ」ができるかを、各toolのsourceで直接確認した。

| tool | probe選択 | 根拠 |
|---|---|---|
| **probe-rs** | **可** | `--probe VID:PID:Serial`(env `PROBE_RS_PROBE`)。`probe-rs/src/probe/wlink/mod.rs`の`get_wlink_info()`が`device.serial_number()`を`DebugProbeInfo`へ格納 |
| **minichlink** | **可** | `-l <serial>` / env `MINICHLINK_LINKE_SERIAL`・`MINICHLINK_programmer_serial_number`。`pgm-wch-linke.c`の`usb_device_matches_serial()`がRV(`1a86:8010`)/ARM(`1a86:8012`)/IAP(`4348:55e0`)の3モードを横断して照合。不一致時は`(available RISC-V: '<serial>')`で候補を列挙 |
| wlink 0.1.2 | 不可 | `-d/--device <INDEX>`のみ(`usb_device.rs`の`open_nth`)。serialは`list_libusb_devices()`が読んで`wlink list`に表示するが選択に使えない |
| WCH OpenOCD | 不可 | 旧コアのrecipeにも選択の口がない |

**minichlinkがWCH-LinkE専用のserial filterを実装している事実は、LinkEが個体ごとのUSB serialを持つ強い傍証**。

この結果、**「既存toolでprobe選択ができないから独自tool」という論拠は成立しない**。
独自`ch32-upload`(Q-044)の判断根拠は、probe選択ではなく
**backend coverage gap**(probe-rsのV407/X315/M030/V205未対応)と配布・UXに絞られる。

wlinkへのserial selector追加は上流patchとして小さい(データは既に読めている)。
probe-rs gapのfallbackにwlinkを使う場合は、この貢献が前提になる。

## 判断ポイント

- probe-rs gap(V407/X315/M030/V205)への対応: target追加をupstreamへ貢献するか、wlink/OpenOCD併用で始めるか(Q-040/Q-044)
- wchispのGPL-2.0: Board Manager tool化は可能(ソース入手先明記)。openwch方式(手動配置)はUXが悪いため採らない
- 互換書き込み器の認定Tier: Tier1=WCH-LinkE(+USB ISP)、Tier2=LinkW/無印Link(対象限定)/minichlink系probe/rv003usb BL、Tier3=実験的(picorvd、ESP32-S3独自実装、Flipper)
- UIAPduinoサポートの形: pnum項目+専用upload.tool定義+復旧手順文書。UIAP側の既存index(openwchベース)との関係整理
- メニュー名は「経路名」(WCH-Link/USB-ISP/UART-ISP/UF2…)で統一し、tool実装名を出さない

## 未検証事項(実機フェーズへ)

- Q-040系の実機認定全般(probe-rs×family×LinkE FW)
- SWエントリ(1200bps touch→BOOT)の各family実機確認(X035は外部実証あり、他は未)
- V407のBOOT0物理ピン、H417のBOOT entry条件(RM精読)
- WCHISPTool_CMDの再配布条件・対応OS
- minichlink系互換probeの実機互換性(Tier2認定)
