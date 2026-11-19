# HostHidReports

[English](README.md)

キーボードやマウスの例が扱わない HID の報告を表示します。ゲームパッドの報告（変わったとき）、メディアのキー（コンシューマー:
音量・再生など）、システムのキー（電源・スリープ）です。どれも、どの機器（アドレス、インターフェース、VID:PID）から来たかを
添えるので、ハブで 2 台つないでも見分けられます。

## 必要なものと配線

- USB ホストの口（USBFS）のある石。機器へ 5 V を出す配線（VBUS）。
- USB のゲームパッド、メディアキーのあるキーボード、またはリモコンなど。
- 115200 bps の UART モニタ（`Serial`）。

## 操作と期待する結果

ゲームパッドのボタンやスティックを動かすと、報告のバイトが変わるたびに 1 行出ます。キーボードの音量のキーを押して放すと
`consumer` の 2 行が出ます。

```
[dev 1 if 0 054c:05c4] gamepad 01 80 80 80 80 08 00 00
[dev 2 if 1 046d:c52b] consumer 0x0e9 pressed
[dev 2 if 1 046d:c52b] consumer 0x0e9 released
[dev 2 if 1 046d:c52b] system 0x82 pressed
```

## API と処理の流れ

| API | この例での使い方 |
|---|---|
| `onGamepad()` | ゲームパッドの報告。`changed` が true のときだけ表示します。並びはパッドごとに違うので、`reportData` の生のバイトを出します。 |
| `onConsumerControl()` | Consumer のページの usage（0xE9 音量上げ、0xCD 再生/一時停止 など）と押した・放した。 |
| `onSystemControl()` | Generic Desktop のページの usage（0x81 電源断、0x82 スリープ、0x83 起床）。 |
| `address` / `interfaceNumber` / `vid` / `pid` | どの機器のどのインターフェースから来たか。 |

通知の関数は `loop()` の `deliverEvents()` から呼ばれます。
