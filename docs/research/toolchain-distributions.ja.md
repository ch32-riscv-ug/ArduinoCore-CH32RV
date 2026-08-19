# R-04: toolchain配布物の調査

調査基準日: 2026-08-19
関連: [Q-020, Q-022, Q-025, Q-026](../open-questions.ja.md)、選定原則は[toolchain方針](../toolchain.ja.md)が正本

## 調査目的

GCCをプロジェクト内でビルド・同梱せず、公開済みの汎用RISC-V GCC配布物をArduino Board Managerのtool参照でそのまま使えるかを確認する。具体的には:

1. 候補配布物(xPack、Espressif/ESP32、riscv-collab、WCH MounRiver)の実態(version、host、ISA対応、ライセンス、保守状況)
2. 全CH32ファミリのISA/ABI要件を満たすか(特にRV32EC/ilp32e)
3. Board Managerからの参照方法とGPL上の義務

## 要件(device databaseで確定済みの事実)

| QingKe core | ISA | 必要march/mabi(目安) | 対象 |
|---|---|---|---|
| V2A | RV32EC | rv32ec(_zicsr)/ilp32e | CH32V003 |
| V2C | RV32EmC | rv32emc(_zicsr)/ilp32e | CH32V002/004/005/006/007/M007 |
| V3A, V4B, V4C | RV32IMAC | rv32imac/ilp32 | V103, V203, V208, X033/X035, L103 |
| V3B | RV32I[M]C[B] | rv32imc(+Zb系)/ilp32 | V205, M030 |
| V4F | RV32IMACF | rv32imafc/ilp32f | V303/305/307/317 |
| V3F | RV32IMAFBC | rv32imafc(+Zb系)/ilp32f | X305/X315, H417(片コア) |
| V3V | RV32IMACB+**Zve64x+Zvbb** | rv32imac+vector拡張 | V407/V467 |
| V5F | RV32IMABCF | rv32imafc(+Zb系)/ilp32f | H417(主コア) |

host要件: Windows x64 / Linux x64(できればarm64)/ macOS x64・arm64。

## 確認済み事実

### 1. 既存コアの現状

- 旧2コア(`arduino_core_ch32_riscv_noneos`/`_arduino`)のpackage JSONは、tools(GCC8 `riscv-none-embed`、GCC12 `riscv-none-elf`、OpenOCD)をすべて`ch32-riscv-ug/MounRiver_Studio_Community_miror` Releases(1.91-toolchain)から取得。**mac arm64はx64バイナリ流用、linux arm64なし**。platform.txtはGCC8+`-M xw`(WCH XW拡張前提)を含む
- ミラーはMRS_Toolchain V1.92(2024-08-08)で停滞。mac用GCC8はxPack 8.2のmacバイナリにWCHライブラリを合成するハック(生成スクリプトに明記)
- openwch公式coreのtoolchainは`openwch/risc-none-embed-gcc`(GCC 8.2.0のみ、ライセンス表示なし、linux/macは個人アカウントのrepository参照、arm64なし)

### 2. 候補比較

| 項目 | **xPack riscv-none-elf-gcc** | WCH MRS(GCC12 fork) | Espressif riscv32-esp-elf | riscv-collab nightly |
|---|---|---|---|---|
| 最新版 | v15.2.0-1(GCC 15.2、2025-10-23)。14.3.0-1/13.4.0-1/12.5.0-1も同日更新 | V1.92(GCC 12.2ベース、2024-08ミラー) | esp-16.1.0(2026-06) | 2026.07.15 |
| host | **5種すべて**(win-x64 zip、linux-x64/arm64、mac-x64/arm64 tar.gz)+sha | win/linux x64、mac(x64流用)。**arm64欠落** | 広い(win-arm64含む) | **Linux x64のみ** |
| **rv32e/ilp32e** | **あり**(`rv32e/ilp32e`、`rv32ec/ilp32e`。14.2.0-3以降で追加を[versioning.sh](https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack/blob/xpack/build-assets/scripts/versioning.sh)とv15.2.0-1リリースノートのmultilib一覧で確認) | あり(実績) | **なし**(multilibは全rv32i系) | 未検証 |
| その他ISA | Debian系フルmultilib(rv32imac/rv32imc/rv32imafc等)。GCC14.3+でZve64x/Zvbb | GCC12のため単一文字B・Zvbb不可。**XW拡張と`WCH-Interrupt-fast`はこのforkのみ** | rv32i系のみ | 広い |
| ライセンス | スクリプトMIT、バイナリGPL系(ライセンス同梱)。「official sources無改変」明言 | **forkのpatchedソース公開を発見できず**(GPL的にグレー) | Apache/GPL、srcを同梱 | GPL |
| 保守 | 活発(旧2020年資産も現存、STM32duinoが現役参照) | 停滞 | 活発 | 活発 |
| 判定 | **◎ 唯一全要件を満たす** | ○ 比較lane限定 | **× CH32V003/V00X系に使用不可** | × host不足 |

- 圧縮アーカイブサイズ: xPack v15.2.0-1は1 host 382〜443MB(全multilib同梱のため大きい)
- **ch32funはupstream汎用GCC(Debianパッケージ、Windowsは`install_xpack_gcc.ps1`=xPack GCC14)で全ファミリ動作実績**があり、WCH forkに依存していない。標準ISA GCCでV003(RV32EC)が実用になる裏付け

### 3. ISA拡張とGCC/binutilsの下限

| 拡張 | 対応下限 | 根拠 |
|---|---|---|
| Zba/Zbb/Zbs | GCC 12 | gcc-12/changes.html |
| Zve64x(-march受理) | GCC 12(実用はintrinsicsのGCC 13、自動vector化はGCC 14) | riscv-common.cc grep、gcc-13/changes.html |
| Zvbb | **GCC 14 + binutils 2.41** | gcc-14/changes.html、binutils 2.41告知 |
| 単一文字`B` | GCC 15 + binutils 2.43(それ以前は`_zba_zbb_zbs`表記) | riscv-common.ccのcombineエントリ |

→ V407/V467(V3V)のvector拡張まで視野に入れるなら**GCC 14.3以上**。vector用multilibは存在しないためライブラリはrv32imac/ilp32へフォールバックする(リンク可、性能はコンパイル対象コードのみ恩恵)。

### 4. Arduino Board Managerのtool参照

- package index仕様: toolは`host`+`url`+`archiveFileName`+`size`+`checksum`。形式は`.zip`/`.tar.gz`/`.tar.bz2`(arduino-cli ≥0.30で`.tar.xz`/`.tar.zst`も)。**URLの参照先に制限はない**
- **前例**: STM32duinoの公式package indexはtool `xpack-arm-none-eabi-gcc`を**xpack-dev-toolsのGitHub Releasesへ直リンク**している(再パッケージなし)。第三者資産直参照の最大の実績
- earlephilhower/arduino-picoは自前ビルドを自リポジトリのReleasesへホスト。openwchはWCH GCC8をtagアーカイブ参照

### 5. GPL遵守の整理

- **(a) 第三者公開URLの直接参照**: 自分はバイナリを配布(convey)しないため、ソース提供義務は配布元(xPack)側。checksum固定により上流差し替えは検知できる。リスクはリンク切れのみ(xPackは2020年資産も現存)
- **(b) 自リポジトリへ再ホスト**: GPLv3 §6の義務が生じる。§6(d)により対応ソースへの明確な案内で対応可能(xPackは公式ソース無改変+ビルドスクリプト公開のため整理容易)。**WCH forkはpatchedソースが公開されておらず(b)は避けるべき**で、比較laneも既存`ch32-riscv-ug`ミラーへの(a)参照が無難

(GPL解釈部分は一般的理解であり法的助言ではない。[vendor-policy](../vendor-policy.ja.md)のSBOM方針と合わせて運用する)

## 推奨(提案)

1. **default toolchain: xPack `riscv-none-elf-gcc`のGitHub Releases直リンク参照(STM32duino方式)**
   - version第一候補は**14.3.0-1**(Zve64x/Zvbb対応と枯れ具合のバランス)。単一文字`B`や最新最適化が必要になったら15.2.0-1
   - ユーザー要望だったESP32 toolchainは**rv32e/ilp32e欠落によりCH32V003/V00X系で使用不可**のため不採用
2. **比較lane: MRS GCC12**(XW拡張・`WCH-Interrupt-fast`・プリビルド資産の検証用)を`ch32-riscv-ug`ミラー直参照で維持。default化はしない([toolchain方針](../toolchain.ja.md)の「compiler forkをdefault前提にしない」と一致)
3. 旧GCC8 laneは旧コア比較が必要な期間のみ維持

この推奨は配布物の選定であり、Q-020の最終決定には[toolchain方針](../toolchain.ja.md)の認定matrix(ch32fun比較、サイズ、ISR等)の実測が必要。

## 判断ポイント

- **直リンク vs 再ホスト**: 直リンクは義務が最小だが、xPack側の資産削除・改名リスクを負う。miscリスク低減のため「index生成時にsize/checksumを固定し、CIで定期的にURL生存確認」を入れるか
- **versionの固定方針**: xPackは同日に複数major系列を更新する(12/13/14/15)。認定済みversionのみをindexへ載せ、更新はADR+認定matrix通過を条件にする
- **-marchの正確な指定**: 新しめのbinutilsではCSR命令に`_zicsr`明示が必要。family別の`build.march`文字列はtoolchain認定時に確定する(例: `rv32ec_zicsr`)。XW拡張(`rv32ecxw`等)はdefaultでは使わない
- **アーカイブサイズ**: 1 host約400MBはBoard Manager経由DLとして大きい。許容するか、必要multilibだけの再パッケージ(=再ホスト、(b)の義務発生)を選ぶかはQ-026の一部として判断
- **linux-arm64対応**をrelease要件に含めるか(xPackなら追加コストゼロ、MRS laneは提供不可)
- V2C(RV32EmC)のmultilib選択: `rv32emc`完全一致のlibはなく`rv32ec/ilp32e`等へのフォールバックになる見込み。libgccの乗算がソフト実装になる影響はサイズ/性能実測で確認

## 未検証事項

- xPack 14.3.0-1の同梱multilib実物確認(`--print-multi-lib`実行。現状はversioning.shとv15.2.0-1ノートからの高確度推定)
- 展開後ディスクサイズとArduino IDE/CLI経由インストールの実測(各OS)
- MRS2(MounRiver Studio II)同梱toolchainの版数・配布条件
- ilp32e×newlibの品質(printf、long long、float format)とcode size(Q-022の実測へ)
- WCHプリビルドlib(TouchKey/BLE等)にXW命令が含まれ、標準ISA toolchainで扱えないケースがあるか
- binutilsのZb系対応下限(2.37前後と推定)
