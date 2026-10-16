# USB PD サンプルの使い方

[English](README.md)

CH32X035/X033 の CC 通信で PD Sink/Source を試します。USB のデータクラスとは別の機能です。PDO の解析 API がビルドできる系列でも、PD ハードウェア定義が無ければ `begin()` は失敗します。

## 接続と電源

X035/X033 は CC1=`PC14`、CC2=`PC15`。ボードの USB-C と CC/Rd/電源回路を確認します。ログは `RX` / `TX` の UART、115200 bps・8N1で、USB CDC ではありません。CC 配線は短くします。

| 例 | ボードの役割 | 追加機器・配線 |
|---|---|---|
| ListProfiles | Sink | PD 充電器。起動後に9 Vを要求 |
| CycleVoltages | Sink | PD 充電器、PA1–GND ボタン。固定電圧を循環 |
| PowerSource | Source | 外部の可変電源、VBUSスイッチ、Source用 CC 回路と実制御 callback |

ListProfiles と CycleVoltages は VBUS の電圧を変えます。VBUS をボードの電源入力に使う場合、そのレギュレータと接続負荷を要求電圧へ対応させます。`voltage()` / `current()` は契約値であり、実測値ではありません。

PowerSource の付属 callback はログのみです。実電源制御へ置き換え、広告 PDO と Rp の電流値を供給回路の能力に合わせてから sink を接続します。WeAct X035 の Sink 用 R4/R5 を残した状態で Source として使用しません。

## ビルドと確認

```sh
arduino-cli compile --fqbn ch32-riscv-ug:ch32rv:CH32X035:pnum=CH32X035F8U6 \
  libraries/CH32RVUsbPd/examples/ListProfiles
```

これはリポジトリルートから、core 導入済みの CLI で使う例です。IDE では「スケッチ例 → CH32RVUsbPd」から開きます。生成 profile の存在は実機確認やその系列の PD 実装を意味しません。

Sink 例は初回契約を約5秒待ちます。`connected()` は CC の接続、`ready()` は PD 明示契約です。Type-C-only の電源は接続しても ready になりません。`request()` の単位は mV/mAで、9 Vは `9000`。PPS を含む接続維持のため `loop()` から `maintain()` を呼びます。

Sink と Source は同じ USBPD ブロックを共有し、一度に一方だけです。USB データの Device 機能とは別に動かせますが、相手が PD 電源とデータホストの両方に対応すること、同じコネクタの電源配線が正しいことを確認します。

## 困ったとき

| 症状 | 確認 |
|---|---|
| begin が失敗 | X035/X033 の実装範囲、Source callback の登録、反対ロールとの Busy |
| connected だが ready でない | 電源が PD 対応か、CC配線、契約待ち時間 |
| request が失敗 | 広告 PDO、mV/mA単位、固定値一致/PPS範囲、対向機器の拒否 |
| PPS が失効 | maintain の継続、長時間の停止 |
| reset後に再起動を繰り返す | 既存契約からの復帰と Hard Reset による VBUS 停止、ボードの給電経路 |
| 契約表示と測定電圧が違う | Source の実制御 callback と電源回路。契約APIは電圧計ではない |

API、PPS の量子化、失敗時の処理は[ライブラリ README](../README.ja.md)を参照してください。Sink は X035 で確認済み、Source はビルド・単体試験段階です。

## サンプル一覧

| 例 | 内容 |
|---|---|
| [CycleVoltages](CycleVoltages/README.ja.md) | ボタン操作で充電器の固定 PDO を順に選び、契約値を UART に表示します。 |
| [ListProfiles](ListProfiles/README.ja.md) | PD 充電器の PDO を一覧表示し、9 V を要求します。読み出し専用の例ではなく、VBUS 電圧が実際に変わります。 |
| [PowerSource](PowerSource/README.ja.md) | PD Source の protocol と、VBUS スイッチ・可変電源を接続する callback の骨格です。そのままでは実電源を制御しません。 |
