# H417 の実験用の道具

CH32H417 評価ボードで、SS を挿したまま書き込みと回復を回すための道具です。使い方と注意は
[docs/h417-bench.ja.md](../../../docs/h417-bench.ja.md) にあります。

| ファイル | 内容 |
|---|---|
| `UsbFallback.h` | 実験用スケッチにコピーして使う。SS で configured にならなければ FS へ移る。UART / CDC の `p` / `f` / `s` |
| `dfu.py` | DFU runtime(DETACH)から DFU モードへ入れて、`.bin` を書く。`uv run --no-project --with pyusb --with libusb-package python dfu.py <bin> [interface]` |
| `uart.py` | `UsbFallback.h` のコマンドを 1 つ送って返事を表示する。`python3 uart.py /dev/ttyACM8 p` |
| `ram_restart.sh` | RAM 上で落ちたアプリの関数を「0 を返すだけ」に書き換えて、複写を飛ばして起動し直す |
