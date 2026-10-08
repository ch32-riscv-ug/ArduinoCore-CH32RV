# R-09 prototype: newlib/ilp32eサイズ計測ベンチ

runtime選択とsizeの扱いは[flash size](../../../docs/flash-size.ja.md)と
[toolchain方針](../../../docs/toolchain.ja.md)を参照してください。

## 目的

newlib full / nano / nano+`_printf_float`の代表機能コストを、統合crt0+own ldの実構成で計測する。toolchain更新時に再実行して回帰を見る。

## 使い方

```sh
uv run tests/sizebench/sizebench.py /tmp/sizebench
```
事前に`uv run tools/index/fetch_tools.py`を一度実行しておけば、環境変数の指定は不要です
(`<repo>/.tools`から探します。設定済みの`CH32RV_*`があればそちらが優先されます)。


結果は`<workdir>/results.md`(case × libc × arch のtext/data/bss表)。

## 構成

- `cases/`: 7ケース(空main、puts、printf %d、printf %f、snprintf、C++ virtual、C++ new/delete)。**new/deleteケースは`int *volatile`でpointerをescapeさせている**(GCCのallocation elisionでnew/deleteが消えるのを防ぐため。除去すると測定が無意味になる)
- `syscalls.c`: 最小newlib syscall stub(実行はしない)
- 計測用ldはscriptが生成(MEMORY 1M/128K。実SKUのflashに収まるかは数値で判断する)
- ISA 2種: rv32emc_zicsr/ilp32e(V00X系)とrv32imac_zicsr/ilp32(V3A/V4系)
