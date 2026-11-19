# CompositeCdcMidi

[English](README.md)

1 台に 2 つの機能を持つ複合機器です。`Console` という名前のシリアル（CDC）と、MIDI の口。どちらも PC から来たものを返します。
機能ごとにバッファと押し戻しを持つので、片方が詰まってももう片方は止まりません（EspUsbDevice の CompositeCdcMidi と同じ）。

## 必要なものと配線

USB デバイスのある石と、PC への USB の線。失敗の表示は 115200 bps の UART モニタ（`RX` / `TX`）。

## 操作と期待する結果

PC にはシリアルのポート 1 つ（インターフェース名 `Console`）と MIDI の入出力が 1 つずつ見えます（製品名は `CDC and MIDI echo`）。
端末で打った文字はそのまま返り、MIDI の出力へ送った音は MIDI の入力から返ります。

## API と処理の流れ

| API | この例での使い方 |
|---|---|
| `CH32RVUsbDeviceCdcSerial console(device, "Console")` | インターフェースの名前つきの CDC。 |
| `console.configure()` / `midi.configure()` | 機能ごとのバッファの大きさ。`device.begin()` の前に。 |
| `console.writeAvailable()` | 送る側の空き。空きの分だけ読んで返すので、取りこぼしません。 |
| `midi.readPacket()` / `writePacket()` | MIDI の packet を 1 つずつ。送れなければ持っておき、次の回に送る。 |

CH32H417 の SuperSpeed で確かめました（2026-11-20）: シリアルの 440 バイトと、MIDI の 200 packet（読みながら一度に書いた 600 も）が
そのまま戻りました。
