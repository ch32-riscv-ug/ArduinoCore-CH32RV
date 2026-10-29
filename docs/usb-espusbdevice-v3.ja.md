# EspUsbDevice v3 との API の合わせ方

2026-10-30 に、ESP32 側の USB デバイスのライブラリ（EspUsbDevice v3、TinyUSB を使わない版）と、このコアの
CH32RVUsbDevice の利用者 API の差を調べ、なるべく小さくしました。

- **比べた ESP 側の資料**: `EspUsbDevice-v3/docs/V3_*_API.ja.md`、`V3_MIGRATION.ja.md`、`V3_CH32RV_API_ALIGNMENT.ja.md`、
  `src/EspUsbDevice.h`。
- **方針**: より正しい方に合わせます。このコアの方が正しいところは、ESP 側への修正依頼にします（3 章）。
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
| Audio | `configure(CH32RVUsbAudioConfig)`（v3 と同じ項目名）。録音の `write()` は入った分を返す。`read()` はサンプルフレームの境界で切る。`clearFormats()`、`currentFormat()`、`capacity()`。`endpointCount()` は使う端点の番号の数 |
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
| Video の既定は Bulk | FS の isochronous は 62 KB/s しか出ない（3 章の 3 も見る） |
| CDC の `write()` は空くまで待つ（上限 `setTxTimeoutMs()`、既定 1000 ms）、`flush()` は送り終えるまで待つ | Arduino の Serial と同じ意味（3 章の 1） |
| Audio の再生のリングがあふれたら古い方を捨てる | 遅れの上限を保つ（3 章の 2） |
| HidVendor の GET_REPORT は reportSize まで 0 で埋めて返す（v3 は送った長さのまま） | report の長さは記述子で決まっている |
| まだ入れていないもの | Audio の複数の rate、DFU の Download |

## 3. ESP 側への修正依頼の候補

どれも、このコアの方が正しいと考えるものです。ESP 側で判断してもらいます。

1. **CDC の `write()` と `flush()` の意味。**
   - v3 の `write()` は、書けた分だけ返してすぐ戻ります。`flush()` は送出を始めるだけです。
   - Arduino の `Serial.print()` は `write()` の戻り値を見ないので、バッファが満杯だと出力が欠けます。
   - このコアは Arduino の Serial に合わせ、`write()` はホストが開いている間は待ち、`flush()` は送り終えるまで待ちます。どちらも上限の時間付きです。
   - 上限の時間の設定の名前は、ESP32 の Arduino の HWCDC と同じ `setTxTimeoutMs()` にしました。
2. **Audio の再生のリングがあふれたときの扱い。**
   - v3 は、未読を残して新しいデータを捨てます。そのため、あふれが続くと遅れがリングの長さまで溜まったままになります。
   - 古い方を捨てれば、遅れは常にリングの長さ以下で、新しい音が鳴ります。
3. **Video の既定の転送方式。**
   - v3 の既定は Isochronous です。
   - S3 は FS で、FS の isochronous は 1 ms に 1 パケットなので、64 B の端点では 62 KB/s しか出ません。
   - FS の機種では Bulk を既定にするか、少なくとも文書で勧めるとよいと考えます。
4. **Video の `submitFrame()` の戻り値。**
   - v3 は `esp_err_t` を返し、id を参照の引数で返します。一方 `writeDirect()` は `EspUsbTransferSubmission` を返します。
   - 同じ借用転送なので、`EspUsbTransferSubmission` にそろえると覚えることが減ります。このコアはそうしました。
5. **DFU の `onDetach`。**
   - このコアでは bool を返し、false で DETACH を断れます。再起動はコアが行います。
   - v3 は void で、コールバックを登録すると再起動もそちらに任されます。
   - 断れる方が、書き込み中のデータを守るのに使えます。
6. **MSC のメディアの交換（両方とも未対応）。**
   - `mediaPresent()` を稼働中に変えても、どちらも UNIT ATTENTION（28h、媒体が変わった）を返しません。
   - そのため、ホストが中身を読み直さないことがあります。両方で足す候補です。
