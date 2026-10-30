# EspUsbDevice v3 との API の合わせ方

2026-10-30 に、ESP32 側の USB デバイスのライブラリ（EspUsbDevice v3、TinyUSB を使わない版）と、このコアの
CH32RVUsbDevice の利用者 API の差を調べ、なるべく小さくしました。

- **比べた ESP 側の資料**: `EspUsbDevice-v3/docs/V3_*_API.ja.md`、`V3_MIGRATION.ja.md`、`V3_CH32RV_API_ALIGNMENT.ja.md`、
  `src/EspUsbDevice.h`。
- **方針**: より正しい方に合わせます。このコアの方が正しいところは、ESP 側への修正依頼にしました（3 章に返事と対応）。
  機能の限界から来るこのコア独自の制約は残します（2 章）。

## 1. v3 に合わせたもの

| 範囲 | 変えたこと |
|---|---|
| Device | `ready()` だけにした（`mounted()` を削除）。開始中の `begin()` は `AlreadyStarted` で断る。remote wakeup は `config.remoteWakeupSupported` で宣言したときだけ記述子に載せ、`remoteWakeupEnabled()` を足した |
| 借用転送の型 | `CH32RVUsbTransferSubmission`、`CH32RVUsbTransferResult`、`CH32RVUsbTransferStatus`、`CH32RVUsbTransferError`、`CH32RVUsbTransferTermination` |
| Vendor | `writeDirect()` は Submission を返し、終端（ZLP）を選べる。借用した配列は `onDirectTxComplete()` で必ず 1 回返す（成功、`BusReset`、`Stopped`、`Cancelled`）。受信の `rxMode`（Stream / Callback / Borrowed / Disabled）、`readDirect()`、`onDirectRxComplete()`、`maxDirectReceive()`、`cancelDirectTx()` / `cancelDirectRx()`。1 回の借用転送の 65535 バイトの上限を外した（`maxDirectTransfer()`）。Print の `availableForWrite()` を実際の空きにした。制御要求に `actualBytes` |
| CDC と Vendor | 共通の基底 `CH32RVUsbDeviceBulkStream`（v3 の EspUsbDeviceBulkStream）に FIFO、`onRx()` / `onRxData()` / `onTxComplete()`、借用転送と取り消しを置き、CDC にも同じものがある。借用転送と `onRxData()` の処理は、それを呼んだスケッチにだけリンクされる。`configure(CH32RVUsbBulkStreamConfig)`（rxCapacity / txCapacity / rxBuffer / txBuffer / streamTermination）。`readCapacity()` / `writeCapacity()` はインスタンスごと。`waitWritable()` は容量を超える要求を断る。Vendor の既定の終端は Continue（ZLP なし）。CDC の `lineCoding()` は値で返す |
| HID | System Control は usage 0x81〜0x83 で受け、記述子の selector 1..3 で送る。Gamepad の hat に Null を付けた。GET_REPORT に答え、HidVendor に `setFeatureReport()`。合成記述子はクラスごとに Push / Pop。boot protocol のマウスは 3 バイト。HidVendor の長すぎる送信は断る。SET_IDLE は report ID ごとに持ち（ID 0 は全部）、周期ごとに今の入力レポートを送り直す（前の送信が終わった時から数える。マウスの移動量は送り直さない）。GET_IDLE は ID ごと。HidVendor も最後に送った入力レポートを GET_REPORT で返す |
| MIDI | ケーブル数の範囲外は `begin()` を失敗させる。送信の cable と CIN を検査し、受信の不正を `receiveErrors()` で数える。`availablePackets()` / `availableForWritePackets()`。`configure(CH32RVUsbMidiConfig)`（ケーブル数、名前、rxPackets / txPackets とスケッチの配列、transferPackets、rxMode の Queue / Callback / Disabled、autoFlush、streamTermination）、`onPacket()`、`flush()`、`receiveCapacity()` / `sendCapacity()` / `transferCapacity()`。受信のキューが 1 パケットより小さくても受けられる |
| MSC | `onWrite()` が無ければ読み出し専用。LBA の範囲外は ILLEGAL REQUEST。読み書きの関数の戻り値は要求どおりでなければエラー。VERIFY(10) |
| CCID | `configure(CH32RVUsbCcidConfig)`（messageBytes、removable）。removable false で通知の端点を省く。`removeCard()` は bool |
| Video | `configure(CH32RVUsbVideoConfig{transport})`。`submitFrame()` / `cancelFrame()` / `onFrameResult()` で、フレームを必ず 1 回返す。フレームの大きさと rate の範囲 |
| Audio | `configure(CH32RVUsbAudioConfig)`（v3 と同じ項目名）。録音の `write()` は入った分を返す。`read()` はサンプルフレームの境界で切る。`clearFormats()`、`currentFormat()`、`capacity()`。`endpointCount()` は使う端点の番号の数。同じ PCM 形状でレートだけ違う形式を `addFormat()` で 8 つまで登録でき、ホストが選ぶ（UAC1 は形式の記述子に一覧、UAC2 はクロックの RANGE）。始めは playback の最初のレート。両方向は同じレートの組。変わると `SampleRateChanged` |
| NCM | `stats()`、`endNetwork()`。MAC と IP の設定は bool を返し、開始後は断る。`configure(CH32RVUsbNetConfig)`（ntbInBytes / ntbOutBytes、txBuffers / rxBuffers とスケッチの配列、maxDatagrams、maxFrameBytes、name）と `ntbInCapacity()` などの照会。送信中に来たフレームは次の NTB にまとめる。受信のバッファが複数なら、onFrame() の間に次の NTB を受ける。`txFrames` は送り終えた時に数える |

## 2. このコアの事情で残した違い

| 違い | 理由 |
|---|---|
| 戻り値は `esp_err_t` でなく bool と `lastError()` | ESP-IDF の型を持ち込まない |
| コールバックは `deliverEvents()` から呼ぶ。登録はいつでもよく、`task()` がある | USB の worker のスレッドが無い協調実行 |
| `std::function` でなく `CH32RVCallback` | ヒープを使わない |
| 既定のバッファはオブジェクトの中。大きくするにはスケッチの配列を渡す | `begin()` で malloc しない（X035 などの RAM） |
| NCM の既定は 2048 バイトの NTB を 1 つずつ（v3 は 3200 バイト、送信 3・受信 2）。バッファの数の 0 は「既定」（v3 は「その方向を使わない」）。最大 8 つずつ、1 つの NTB に 32 フレームまで | RAM。0 の意味は CDC / Vendor / MIDI と同じ |
| MIDI の rxPackets / txPackets の 0 は「クラスのキューをそのまま使う」（v3 の 0 は「その方向を使わない」）。受信しないなら rxMode Disabled | 0 で既定を表すのは CDC / Vendor の容量と同じ |
| 転送の id は 32 ビット | RAM。v3 は 64 ビット |
| 借用転送は境界の条件なし。受信の容量はパケットの倍数だけ（64 の倍数は求めない） | DMA の 64 バイト境界は ESP の制約 |
| 取り消しは呼び出しの中で終わり、`Cancelled` もその中で返る。同じ転送をもう一度取り消すと false | worker のスレッドが無く、controller の転送はその場で止められる |
| `onRxData()` は実行時に付け外しでき、Stream のままでも転送ごとに渡す。Callback は「begin() の時に onRxData() が要る」だけの違い | 協調実行なので途中で切り替えても競合しない |
| Video の既定は Bulk | このコアの USBFS は端点が 64 バイトまでで、isochronous は 1 ms に 64 バイト（62 KB/s）しか出ない。v3 の FS の isochronous は 512 バイトのパケットなので、v3 は Isochronous のまま（3 章の 3） |
| CDC の `write()` は、ホストが端末を開いていない（DTR が無い）間は捨てる（v3 は DTR を見ずに送る） | 開いていない UART と同じで、待ちもしない（3 章の 1） |
| HidVendor の GET_REPORT は reportSize まで 0 で埋めて返す（v3 は送った長さのまま） | report の長さは記述子で決まっている |
| Audio の形式は 1 ストリームに PCM 形状 1 つ（レートは 8 つまで）。v3 は形状の違う形式を alternate setting で切り替える | まだ入れていない |
| まだ入れていないもの | Audio の PCM 形状ごとの alternate、DFU の Download |

## 3. ESP 側への修正依頼と返事

このコアの方が正しいと考えた 6 点を ESP 側に送り、2026-10-30 に返事がありました
（`V3_CH32RV_API_ALIGNMENT.ja.md` の「CH32RV側からの6点の再検討」）。

| 項目 | 依頼 | ESP 側の判断 | このコアの対応 |
|---|---|---|---|
| 1. CDC の `write()` と `flush()` | Arduino の Serial に合わせ、`write()` は空くまで、`flush()` は送り終えるまで待つ（上限付き） | 採用。`setTxTimeoutMs()`（既定 1000 ms、0 は待たない）と `txTimeoutMs()`。借用送信の間は `availableForWrite()` が 0。DTR は求めない | `txTimeoutMs()` を足した。借用送信の間は `writeAvailable()` / `availableForWrite()` を 0 にし、`write()` もその間は受けない（CDC と Vendor）。DTR が無い間に捨てるのは違いとして残す（2 章） |
| 2. Audio の再生のリングがあふれたとき | 新しいデータを捨てず、古い方を捨てる | 生の再生には古い方を捨てる方が自然。捨てる単位はサンプルフレーム、パケットがリングより大きいときは新しい側を残し、量を数える。録音などのために残す方も選べるようにする案 | 前から古い方を捨てていた。ESP の仕様案（V3_AUDIO_API.ja.md）に合わせ、`playbackOverflowPolicy`（`CH32RVUsbAudioOverflowPolicy::DropOldest` が既定、`DropNewest` で前を残す）を足し、パケット末尾の半端なフレームは捨てるようにした。あふれたパケットごとに `overrunCount` を 1、捨てた・押し出したバイトを `overrunBytes` に数える |
| 3. Video の既定の転送方式 | FS では Bulk を既定に | 変えない。v3 の FS の isochronous は 64 バイトでなく 512 バイトのパケット（1 ms に 1 つ） | 依頼の前提が誤りだったので取り下げた。このコアの既定 Bulk は、USBFS の端点の 64 バイトの限界から来る違いとして残す（2 章） |
| 4. Video の `submitFrame()` の戻り値 | 借用転送の Submission にそろえる | 採用 | 同じ名前と型になった |
| 5. DFU の `onDetach` | bool で DETACH を断れるように | false を返しても、もう成功を返した要求を断ったことにはならない。受け付けるかどうかは SETUP の時に決め、断るなら STALL にする案 | その案にした。`onDetach()` は DFU_DETACH の SETUP の時に呼び、false なら要求を STALL で断る（状態も変えず、再起動もしない）。true なら status の後に移る。ESP の仕様案（V3_DFU_API.ja.md）の `setDetachHandler()`（移り方をアプリが持つ）は ESP 独自なので置かない。登録は ESP と違って稼働中でもよい（協調実行で競合しない） |
| 6. MSC の媒体の交換 | `mediaPresent()` の変化を UNIT ATTENTION で知らせる | 賛成。稼働中の false→true を交換として、次のコマンドに 1 度だけ。INQUIRY と REQUEST SENSE は除く。同じ値の再設定は交換としない | その形で入れた。ホストが構成している間に false→true になると、次のコマンド（INQUIRY と REQUEST SENSE を除く）に UNIT ATTENTION 06/28/00 を 1 度返す。ESP の仕様案（V3_MSC_API.ja.md）に合わせ、知らせる前にまた抜かれても 1 度は知らせ、USB リセットと `end()` で保留と sense を消す（BOT の reset recovery では消さない）。読み書きの途中で抜かれたら、残りを 02/3A/00（媒体なし）で失敗させる（ESP も「失敗が妥当」と返事）。今の 1 ブロックは最後まで行い、書けた分は戻さない。コマンドの始まりから一度でも抜かれたら、挿し直されていても残りは続けない（抜いた回数を覚えて比べる）。知らせていない交換は、この失敗では消えない |
