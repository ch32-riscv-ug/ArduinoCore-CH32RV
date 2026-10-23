# MSCSdCard

English: [README.md](README.md)

SPI の SD カードを USB MSC の媒体として PC へ公開します。ファイルシステムは PC が扱います。

## 必要なものと配線

3.3 V の SD ソケット/対応モジュール、カード、USB Device 対応ボード。SCK/MISO/MOSI は同名のボード既定 pin、CS は通常 PA4（PA4 がない系列では PC4）。電源と GND も接続します。

## 操作と期待する結果

1. 自分の配線に `SCK_PIN` / `MISO_PIN` / `MOSI_PIN` / `CS_PIN` を合わせます。
2. UART に `USB SD MSC ready blocks=... block_size=512` が出ることを確認します。
3. PC で既存ファイルを読み書きし、取り外し操作で `SD_EJECT` を確認します。

## 設定と制約

SPI は4 MHzで開始します。`SD_BEGIN_FAILED` なら先に電源・CS・SPI 配線を確認します。カードへ直接ブロックを書き込むため、PC が使用中にボード側の別 filesystem から同じ媒体を変更しないでください。先にバックアップできるテスト用カードを使います。

## API の使い方と改造のポイント

| API | この例での使い方 |
|---|---|
| `SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, -1)` | SD に使う SPI の端子を指定します。CS は次の API へ別に渡します。 |
| `sdMsc.begin(CS_PIN, SPI, clockHz)` | SD を SPI モードで初期化します。周波数の単位は Hz です。 |
| `sdMsc.attach(msc)` | SD のブロック読み書きを USB MSC へ結び付けます。 |
| `sdMsc.onEject(...)` | ホストの取り外しを通知します。通知を受けるには `deliverEvents()` を呼びます。 |

初期化は SPI→SD→MSC→USB の順です。`SD_BEGIN_FAILED` なら USB の問題を調べる前に、電源、CS、
MISO/MOSI/SCK の経路とカードを確認します。配線が長い場合などは、SPI 周波数を下げて切り分けます。

ファイルシステムを扱うのは PC です。同じカードを MCU 側のファイルシステムから同時にマウントすると、
キャッシュや管理情報が食い違う原因になります。ホストが安全に取り外し、使用権を移してから MCU 側で扱います。

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbDevice の API と対応範囲](../../README.ja.md)
- [USB 全体のガイド](../../../../docs/usb.ja.md)

[MSCSdCard.ino](MSCSdCard.ino)
