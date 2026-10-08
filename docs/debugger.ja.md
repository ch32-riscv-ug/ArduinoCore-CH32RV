# GDB デバッガ

`arduino-cli debug` は source breakpoint、step、backtrace、変数の読み書きに使えます。
この platform は GDB と設定 file `debug/ch32-riscv.cfg` を提供しますが、GDB server の OpenOCD は同梱しません。

## OpenOCD を同梱しない理由

mainline OpenOCD は WCH-Link の RISC-V interface を扱いません。利用できる WCH fork は対応 source を通常の公開 archive
として配布しておらず、この project が GPL binary を再配布すると対応 source の提供責任も負います。そのため、
利用者が MounRiver Studio または別の CH32 環境で導入した OpenOCD を明示指定します。

Linux で WCH OpenOCD が `libjaylink.so.0` を要求する場合は、同じ配布物の library directory を
`LD_LIBRARY_PATH` に加えます。

## OpenOCD の指定

`platform.txt` は編集しません。更新で置き換わるためです。

```sh
# shell 単位
export PATH="<openocd-bin>:$PATH"

# command 単位
arduino-cli debug \
  --debug-property server.openocd.path=/path/to/openocd \
  --fqbn ch32-riscv-ug:ch32rv:CH32V003:pnum=CH32V003F4P6 \
  -P wch-link ./MySketch
```

repository を Arduino hardware directory として直接使う場合は、xPack GDB の directory も指定します。

```sh
arduino-cli debug \
  --debug-property toolchain.path=/path/to/xpack/bin/ \
  --debug-property server.openocd.path=/path/to/openocd \
  --fqbn ch32-riscv-ug:ch32rv:CH32V003:pnum=CH32V003F4P6 \
  -P wch-link ./MySketch
```

恒久的な local override は platform root の `platform.local.txt` に置けます。

```text
debug.server.openocd.path=/path/to/openocd
```

## Debug session

```sh
arduino-cli compile \
  --fqbn ch32-riscv-ug:ch32rv:CH32V003:pnum=CH32V003F4P6 ./MySketch
arduino-cli debug \
  --fqbn ch32-riscv-ug:ch32rv:CH32V003:pnum=CH32V003F4P6 \
  -P wch-link ./MySketch
```

WCH OpenOCD は attach 時に reset 停止用の instruction を flash 先頭へ書くため、GDB prompt が出たら必ず
`load` で sketch を書き戻します。

```text
(gdb) monitor reset halt
(gdb) load
(gdb) break loop
(gdb) continue
```

debug 終了後の board には最後に `load` した image が残ります。通常の書き込みへ戻す場合は
`arduino-cli upload` を実行します。

## よくあるエラー

| 症状 | 確認すること |
|---|---|
| `Debugging not supported for board` | 正しい platform / FQBN を使っているか |
| `GDB server ... is not supported` | arduino-cli の server 名は `openocd` か |
| toolchain path のエラー | xPack の `bin/` を `toolchain.path` に渡したか |
| `libjaylink.so.0` が無い | WCH OpenOCD と同じ配布物の library path を設定したか |
| `WCH-Link not found` | 他 process が probe を保持していないか |
| breakpoint 後に正常実行しない | session 開始後に `load` したか |
