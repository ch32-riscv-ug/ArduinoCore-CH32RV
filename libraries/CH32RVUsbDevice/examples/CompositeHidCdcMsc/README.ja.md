# CompositeHidCdcMsc

English: [README.md](README.md)

キーボード、CDC、10 KiB FAT RAM ディスクを一つの USB デバイスにまとめます。CDC からキーボード入力を指示できます。

## 必要なものと配線

PC の CDC 端末、テキストエディタ、ファイルブラウザ、UART モニタ。RAM ディスクと USB buffer が収まる型番を使います。

## 操作と期待する結果

1. CDC、HID、`CH32RV` ボリュームが同時に現れることを確認します。
2. CDC に普通の文字列と改行を送ると `echo: ...` が返ります。
3. `type Hello` と改行を送ると、現在フォーカスのある画面にキーボードとして `Hello` と改行が入力されます。
4. CDC には約5秒ごとに `tick=...` が届きます。

## 設定と制約

ホストの入力先を先にテスト用エディタへ切り替えます。CDC のコマンドバッファは128 bytesで、長い行は先頭127文字までです。RAM ディスクは reset で消えます。endpoint は HID 1、CDC 2、MSC 1 の計4番号を使い、ESP32 の IN endpoint 上限の説明は CH32 に当てはめません。

## API の使い方と改造のポイント

| API | この例での使い方 |
|---|---|
| 各 function の `...(device)` | キーボード・CDC・MSC を一つのデバイスへ追加します。 |
| `disk.format()` / `disk.addTextFile()` / `disk.attach(msc)` | RAM にファイルシステムを用意し、MSC の読み書き先へ結び付けます。USB 起動前に行います。 |
| `UsbSerial.read()` | 受信バイトを改行まで蓄積し、行が完成してからコマンドを判断します。 |
| `keyboard.write()` | `type ` に続く文字列を USB キーボードとして送ります。CDC への返信とは送信先が違います。 |

CDC はストリームなので、一回のホスト送信が一回の `read()` に対応するとは限りません。改行まで保持する
`lineBuffer` が、USB の転送区切りとは独立したコマンド境界を作ります。実アプリでは長すぎる行を黙って切る代わりに
エラーとして扱うと、途中までのコマンドを誤実行せずに済みます。

PC がマウント中のディスクをスケッチから同時に変更すると、ホストのキャッシュと食い違います。
ファイル内容を扱う処理を足す場合は、取り外し通知を使う [MSCFatRamDisk](../MSCFatRamDisk/README.ja.md) も参照します。

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbDevice の API と対応範囲](../../README.ja.md)
- [USB 全体のガイド](../../../../docs/usb.ja.md)

[CompositeHidCdcMsc.ino](CompositeHidCdcMsc.ino)
