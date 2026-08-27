# GDBデバッガの使い方

文書基準日: 2026-08-27

`arduino-cli debug`でCH32を**source単位でデバッグできます**(breakpoint、backtrace、
変数の読み書き、step)。**このcoreはGDB serverを同梱していません**。
手元にあるものを指して使う形なので、その手順です。

出力を眺めるだけなら[デバッグ出力の受け取り方](debug-output.ja.md)のほうが手軽です。

## なぜ同梱しないのか

- **stockのOpenOCDでは動きません。** mainlineのadapter driver一覧に
  `wlink`/`wlinke`が無く、WCH-LinkをRISC-Vモードで駆動できません
  (`ch347`と`cklink`はありますが別物です)
- **動くのはWCHのforkだけ**で、**WCHはそのソースを公開していません**
  (`openwch/openocd_wch`はWindowsバイナリの詰め合わせで、READMEは
  「ソースはsupport@mounriver.comへ連絡」)。GPLのバイナリを再配布すると
  対応ソースの提供義務がこちら側に来ます
- なので当面は**PATHから拾う**方針です(`compiler.path`と同じ考え方)。
  自前ビルドのforkを配る案は別途検討中です

同梱しているのは**設定ファイルだけ**です(`debug/ch32-riscv.cfg`)。

## 1. OpenOCDを用意する

WCHのforkが要ります。すでに入っているものが使えます。

| 入手元 | 場所の例 |
|---|---|
| **MounRiver Studio**(Linux/Windows/macOS) | `<MRS>/OpenOCD/OpenOCD/bin/openocd` |
| **WCHのArduino core**(Board Managerで入れたもの) | `~/.arduino15/packages/WCH/tools/openocd/1.0.0/bin/openocd` |
| 他のCH32 Arduino core | `~/.arduino15/packages/<vendor>/tools/openocd/*/bin/openocd` |

**Linuxでは`libjaylink.so.0`が要ります。** distroに無ければ、MounRiver Studioが
同梱しているものを使ってください。

```sh
export LD_LIBRARY_PATH=<MRS>/resources/app/resources/linux/components/WCH/Others/CommunicationLib/default
```

## 2. coreに場所を教える

3通りあります。**platform.txtは編集しないでください**(更新で消えます)。

```sh
# a. PATHに置く(既定はこれを期待しています)
export PATH="<openocdのbinディレクトリ>:$PATH"

# b. 1回だけ指定する
arduino-cli debug --debug-property server.openocd.path=/path/to/openocd ...

# c. 恒久的に指定する - platform.txtの隣に platform.local.txt を作る
echo 'debug.server.openocd.path=/path/to/openocd' >> platform.local.txt
```

手動インストール(gitのcloneをそのままplatformにしている)の場合は、
toolchainのpathも渡してください。Board Managerで入れた場合は自動で解決します。

```sh
arduino-cli debug --debug-property toolchain.path=<xpack>/bin/ ...
```

## 3. デバッグする

```sh
arduino-cli compile --fqbn ch32-riscv-ug:ch32v:CH32V003:pnum=CH32V003F4P6 ./MySketch
arduino-cli debug   --fqbn ch32-riscv-ug:ch32v:CH32V003:pnum=CH32V003F4P6 -P wch-link ./MySketch
```

gdbのプロンプトが出たら、**まず`load`を打ってください**。

```
(gdb) monitor reset halt
(gdb) load
(gdb) break loop
(gdb) continue
```

### `load`は省略できません

WCHのOpenOCDは**attachした時点でflashの先頭を書き換えます**。
アドレス4から48 byteのnopと、0x34に`ebreak`を置きます
(「resetして止める」ための仕掛けで、**元に戻しません**)。
そのまま走らせると最初の割り込みでnopを踏み抜いて`ebreak`で止まります。
`load`すればsketchが上書きし直されるので正常に戻ります。
MounRiver StudioもEclipseも毎回loadする設定なので、これがIDEでの通常の流れです。

**デバッグをやめたあと、boardには最後にloadしたものが載っています。**
書き込み直したいときは`arduino-cli upload`をもう一度実行してください。

### 速度

`load`は**1 KB/s程度**しか出ません。3 KBのsketchで数秒〜十数秒かかります。
probe-rsの書き込み(数秒)と比べると遅いので、**普段の書き込みはuploadを、
デバッグしたいときだけ`load`を**使ってください。

## 動作確認したもの

| | 結果 |
|---|---|
| CH32V003 + WCH-LinkE | `load`→`break`→`continue`→変数の読み出しまで確認 |
| CH32V103 + 初代WCH-Link(CH549) | `load`→`tbreak handle_reset`→`continue`で停止を確認 |

**初代WCH-Link(CH549)でもデバッグできます。** SDI printと違ってLinkE専用ではありません。

## うまくいかないとき

| 症状 | 原因 |
|---|---|
| `Debugging not supported for board` | `debug.*`が読めていない。platformの入れ方を確認 |
| `GDB server 'xxx' is not supported` | arduino-cliは`openocd`しか受け付けません |
| arduino-cliが**panic**する | `debug.toolchain.path`が空。上の`--debug-property`で渡してください |
| `libjaylink.so.0: cannot open shared object file` | 1章の`LD_LIBRARY_PATH` |
| `Error: WCH-Link not found` | probeが他のプロセス(WCH-LinkUtility、wlink、minichlink)に掴まれています |
| breakpointは当たるのに`continue`で先に進まない | `load`していません。上の説明を参照 |

## OpenOCDを使わない方法

minichlink(ch32fun)にもGDB serverがあり、**CH32V003では完全に動きます**
(上流も「003が主戦場、他は限定的」と書いています)。
flashを書き換えないので`load`も要りません。

```sh
minichlink -G                                   # 別の端末で
riscv-none-elf-gdb -ex "target remote :3333" ./build/MySketch.ino.elf
```

`arduino-cli debug`から呼ぶことは**今はできません**(arduino-cliが`openocd`しか
知らないため)。ただし調査の結果、arduino-cliがserverに要求しているのは
「上のコマンドラインを受け取り、GDB remote protocolを**標準入出力で**話す」ことだけなので、
**minichlinkを裏で動かす小さな仲介プログラムを`openocd`として登録すれば繋がります**
(手元では実際に繋がることを確認済み)。配布物として持つかは検討中です。
