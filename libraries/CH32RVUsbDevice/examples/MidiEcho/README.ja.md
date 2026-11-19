# MidiEcho

[English](README.md)

PC から来た USB MIDI の packet を、そのまま返します。IN の待ち行列がまだ受けられない packet は持っておき、送れてから次を
読むので、取りこぼしません（EspUsbDevice の MidiEcho と同じ）。

## 必要なものと配線

USB デバイスのある石と、PC への USB の線。失敗の表示は 115200 bps の UART モニタ（`RX` / `TX`）。

## 操作と期待する結果

DAW や MIDI のモニタの道具で、ボードの MIDI の出力へ音や CC を送ると、同じものがボードの MIDI の入力から返ります。速く
たくさん送っても、順番どおりに全部返ります。

## API と処理の流れ

| API | この例での使い方 |
|---|---|
| `midi.configure(config)` | cable の数と、受信・送信の待ち行列の packet の数。`device.begin()` の前に。 |
| `midi.readPacket(packet)` | 受けた packet を 1 つ取り出す。無ければ false。 |
| `midi.writePacket(packet)` | 送る。待ち行列が満杯なら false で、その packet を持ったまま次の回に送り直す。 |
| `device.ready()` | ホストが設定を選んだか。抜かれたら持っていた packet を捨てる。 |

CH32H417 の SuperSpeed で確かめました（2026-11-20、PyUSB で USB-MIDI の packet を直接）: 16 個ずつの 200 個と、読みながら一度に
書いた 600 個が、どれも順に戻りました。PC が読まない間は、デバイスが書き込みの残りを受けずに待たせ（PC の書き込みが待つ）、
捨てません。
