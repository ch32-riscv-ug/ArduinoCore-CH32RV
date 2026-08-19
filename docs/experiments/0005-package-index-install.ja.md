# 実験0005: package index生成とローカルHTTP経由のclean install

実施日: 2026-08-19
対象question: Q-026(tool参照方式)、Q-054(index配信)、R-15方式B
実装: [tools/index/](../../tools/index/README.ja.md)
実施環境: WSL2 Linux x86_64、arduino-cli、xPack riscv-none-elf-gcc 14.3.0-1(実験0001と同一archive)。実機なし

## 目的

利用者と同じ経路(package index → tool DL → platform DL → compile)を、公開サーバなしでローカル一周させる。特に「xPack GitHub Releases直リンクをtoolとして参照できるか」(R-04推奨の前提)を、archive実物で確認する。

## 方法

1. xPack 14.3.0-1の**6 host分のtool定義fragment**を作成(GitHub直リンクURL、公式`.sha`のchecksum、GitHub APIのsize。linux-x64は実験0001のhashと一致することを相互検証)
2. `gen_index.py`がprototype platform(実験0003/0004)を`.tar.bz2`化し、index JSONを生成。パッケージ時にplatform.txtの`compiler.path=`を`{runtime.tools.xpack-riscv-none-elf-gcc.path}/bin/`へ書き換え
3. テストではtool URLだけローカルHTTPへ差し替え(checksumは公式のまま。400MB再DL回避)、`python3 -m http.server`で配信
4. 新規`ARDUINO_DIRECTORIES_*`(clean環境)で`core update-index` → `core install ch32-riscv-ug:ch32v` → **compiler.path上書きなしで**Blink compile

## 結果: 全経路成功

- index取得 → tool 395MiB DL+checksum検証+install → platform 5.72KiB DL+install → `core list`に`ch32-riscv-ug:ch32v 0.0.1`
- **上書きなしのcompile成功**(Blink 440B / max 63488B)。つまり:
  - arduino-cliは**tool archiveの単一rootフォルダを平坦化**し、`{runtime.tools...path}/bin/`が正しくgccを指す → **xPackアーカイブは再パッケージ不要**(STM32duino方式の実証)
  - packager配下のtool定義(`ch32-riscv-ug:xpack-riscv-none-elf-gcc@14.3.0-1`)+`toolsDependencies`の解決も正常
- 途中の失敗1件: 配信ファイル名がindexの`archiveFileName`と不一致だと404でinstall失敗(当然だが、index生成と配信物の名前整合はCIで検証すべき項目)

## 結論

- **R-15方式B(ローカルHTTP+arduino-cliによる実インストール経路の検証)は成立し、script一発で再現できる**
- R-04の推奨「xPack GitHub Releases直リンク参照」は、archive形式・tool解決の両面で実物確認済みになった。残るリスクはGitHub側の可用性のみ
- 開発時(symlink+PATH/上書き)とパッケージ時(`{runtime.tools...}`)でcompiler.pathを切り替える方式は、gen_index.pyのパッケージ時書き換えで両立できる

## 再現手順

```sh
CH32_XPACK_ARCHIVE=<xpack-riscv-none-elf-gcc-14.3.0-1-linux-x64.tar.gz> \
tools/index/test_install.sh /tmp/w5-work
# INSTALL-AND-COMPILE OK で終了
```

## 残る未検証事項

- Windows/macOS/linux-arm64 hostでの同検証(GitHub Actions matrix、W-6)
- `--tools github`(実GitHub直リンク)でのinstall実測(回線・可用性確認)
- index正式配信(GitHub Pages)とappend-only運用、beta/stable導線(Q-054)
- IDE 2.x GUIからのinstall
