# UsbReconnect

[English](README.md)

ホストがいなくなっても続く USB CDC の記録係です。200 ms ごとの値を、端末がポートを開いていない間は小さな輪（32 個）に貯め、
開き直されたら古い順に送ります。輪からあふれて失った数も知らせます。バスの出来事（リセット、設定、休止、再開）は
ハードウェアの UART に表示します。

## 必要なものと配線

- USB デバイスのある石と、PC への USB の線。
- 115200 bps・8N1 の UART モニタを `RX` / `TX` に（USB の側は現れたり消えたりするので、表示はこちら）。

## 操作と期待する結果

1. PC の端末で USB のシリアルを開くと `reading 0`、`reading 1` … が 200 ms ごとに届きます。
2. 端末を閉じて 3 秒ほど待ち、開き直すと、まず貯めた分が届きます。

   ```
   resumed: 15 kept, 0 lost while closed
   reading 37 (kept)
   ...
   ```

   10 秒ほど閉じたままにすると、32 個を超えた分が `lost` に数えられます。
3. USB の線を抜いて挿し直すと、UART に `bus: reset`、`bus: configured` などが出ます。PC がスリープすると `suspended`。

## API と処理の流れ

| API | この例での使い方 |
|---|---|
| `SerialUSB.connected()` | 端末がポートを開いている（DTR が上がっている）間 true。閉じている間は送らずに輪へ貯めます。 |
| `SerialUSB.setTxTimeoutMs(50)` | 読まなくなったホストを待つ上限。過ぎると、線が開いたままのときと同じく捨てます。 |
| `USBDevice.onBusEvent()` | リセット・設定・休止・再開を、起きた順に `deliverEvents()` から受けます。 |

閉じていたことの判断は DTR です。DTR を上げない端末（一部の道具）では、開いても `connected()` が false のままです。

CH32H417 の SuperSpeed で確かめました（2026-11-20）: 3 秒閉じて `resumed: 15 kept, 0 lost`、10 秒で `32 kept, 18 lost`、
取っておいた分が先に来ました。`resumed` の行はポートを開いた瞬間に来るので、pyserial の `open()`（最後に入力を空にする）や
TCSAFLUSH で raw にする開き方では消えます。ターミナルのソフトでは見えます。
