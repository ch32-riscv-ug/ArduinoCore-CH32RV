# W-5 prototype: package index生成とclean install検証

状態: proof of concept(2026-08-19)。リリース対象ではありません。
関連: [環境整備計画](../../docs/infrastructure.ja.md) W-5、[R-15方式B](../../docs/research/local-install-and-test-env.ja.md)、[R-04](../../docs/research/toolchain-distributions.ja.md)、[実験0005](../../docs/experiments/0005-package-index-install.ja.md)

## 目的

Board Manager経由の利用者経路(index → tool → platform → compile)を、実機・公開サーバなしでローカル再現する。

## 構成

| ファイル | 内容 |
|---|---|
| `tools_xpack_gcc.json` | xPack GCC 14.3.0-1のArduino tool定義fragment(6 host entry、GitHub Releases直リンク、公式.sha由来のchecksum、API由来のsize。2026-08-19確認) |
| `gen_index.py` | platformディレクトリを`.tar.bz2`化し、`package_ch32-riscv-ug_index.json`を生成。パッケージ時にplatform.txtの`compiler.path=`を`{runtime.tools.xpack-riscv-none-elf-gcc.path}/bin/`へ書き換える(作業treeはsymlink開発用にPATH/上書き既定のまま) |
| `test_install.sh` | ローカルHTTP配信 → 新規data dirへ`core update-index`/`core install` → **上書き指定なしで**Blink compile。tool URLだけローカルキャッシュへ差し替え(checksumは公式のまま)、GitHubからの400MB再DLを回避 |

## 使い方

```sh
CH32_XPACK_ARCHIVE=/path/to/xpack-riscv-none-elf-gcc-14.3.0-1-linux-x64.tar.gz \
./test_install.sh /tmp/w5-work
# 最後に INSTALL-AND-COMPILE OK
```

`--tools github`でgen_index.pyを使うと、tool URLをxPack GitHub Releases直リンクにした「本番形」のindexを生成できる(installにはGitHubから実DLが必要)。

## 検証済みの重要事実

- arduino-cliはtool archiveの単一rootフォルダを平坦化し、`{runtime.tools.<name>.path}`が`bin/`直上を指す → **xPackアーカイブを再パッケージなしでtool参照できる**(STM32duino方式の追認)
- tool定義はpackager配下(`ch32-riscv-ug:xpack-riscv-none-elf-gcc`)で問題なく解決される
- platform archiveは5.7KB(コアがまだスタブのため)。checksum/size検証も通過

## 今後

- indexの正式配信(GitHub Pages/Releases)とappend-only運用はW-6以降
- win/mac/linux-arm64 hostでの同検証はGitHub Actions matrixで
