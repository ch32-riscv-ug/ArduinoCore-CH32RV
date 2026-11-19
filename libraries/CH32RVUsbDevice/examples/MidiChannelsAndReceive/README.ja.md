# MidiChannelsAndReceive

[English](README.md)

USB MIDI の口（cable）を 2 つ持つ機器です。MIDI の channel（1〜16）と USB の cable（口）の違いを示します。口 1 で受けた
音の強さで LED を光らせ、受けたメッセージを cable と channel つきで UART に表示し、音は来た channel のまま口 2 から返します。

## 必要なものと配線

- USB デバイスのある石と、PC への USB の線。
- `LED_PIN`（既定 `PA1`）に抵抗つきの LED。PWM の出る端子なら強さが明るさになります。
- 115200 bps・8N1 の UART モニタを `RX` / `TX` に。

## 操作と期待する結果

PC には MIDI の入力と出力が 2 つずつ見えます（名前は OS で違います）。DAW や MIDI のモニタの道具で、1 つ目の出力へ
channel 3 の Note On を送ると、LED が光り、UART に次のように出て、2 つ目の入力に同じ音が channel 3 で届きます。

```
USB MIDI with 2 ports ready
RX port 1 channel 3 note-on 60 100
RX port 1 channel 3 note-off 60 0
```

2 つ目の出力へ送ったものは表示されるだけです（`RX port 2 ...`）。

## API と処理の流れ

- `CH32RVUsbDeviceMidi MIDI(device, 2, 2)` で、入出力とも cable を 2 本にします。cable は USB の口で、ホストには別の MIDI の
  口として見えます。channel は MIDI のメッセージの中の番号で、1 つの口に 16 あります。
- 受けた packet の header の上位 4 bit が cable、status byte の下位 4 bit が channel です（API の channel は 0 から）。
- `noteOn(channel, note, velocity, cable)` の最後の引数で送る口を選びます。

CH32H417 の SuperSpeed で確かめました（2026-11-20、PyUSB で USB-MIDI の packet）: 口 1・channel 3 のノートオンとオフが、口 2・
channel 3 で戻り、口 2 のノートとコントロールチェンジは返りませんでした。
