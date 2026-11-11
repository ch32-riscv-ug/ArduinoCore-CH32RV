# peer/usb — ESP32-S3 / RP2040 との USB 対向試験

スケッチは規約どおり tests/sketch_support に置き、run_hardware.py がケースの作業ディレクトリへ
`peer_s3/` か `peer_pico/` として並べます（pytest-embedded-arduino-cli の peer の置き方）。どちらを使うかは、
ベンチの記録（records の usb.to）で決まります。`esp32-s3` なら S3、`rp2040` なら Pico です（下の「RP2040（Pico）を相手にする」）。
`esp32-p4` なら P4 が S3 と同じスケッチを、sketch.yaml の p4 の設定（OTG HS の口）で動かし、DUT の USBHS と HS で
つながります（ホストの case では DUT が `CH32RV_USB_HOST_PORT_HIGH_SPEED` で始めます）。

DUT（CH32）の USB ポートを ESP32-S3 の USB OTG ポートにつなぎ、役割ごとに別のケースとして試験します
（[テスト計画](../../TEST_PLAN.ja.md) の「USB peer 計画」）。S3 側は独立した実装（EspUsbHost は ESP-IDF の
USB ホスト、EspUsbDevice は TinyUSB を S3 の OTG で動かすもの）なので、通ることが相互運用の確認になります。

| ケース | DUT の役割 | DUT スケッチ（sketch_support/） | S3（sketch_support/usb_peer/） | 確かめること |
|---|---|---|---|---|
| usb-peer | デバイス | usb_contracts | device/peer_s3（EspUsbHost） | 列挙、CDC（往復・2000 バイト・ボーレート）、キーボード、ベンダー HID、LED、MIDI、再接続、DFU_DETACH → 工場 ISP → 復帰 |
| usb-peer-classes | デバイス | usb_classes | device/peer_s3 | ベンダーバルクと要求（FIFO の経路と、`onRxData()` + `writeDirect()` の経路、`setInterfaceProtocol()` の subclass）、MSC（INQUIRY・容量・ブロックの読み書き・FAT 上のファイルの読み書き）、CCID（ATR・APDU） |
| usb-peer-media | デバイス | usb_media（カメラはアイソクロナス） | device/peer_s3 | スピーカー（合計値の一致）、カメラ（パターン）、マイク（連番） |
| usb-peer-net | デバイス | usb_net | device/peer_s3 | NCM のフレームの往復 |
| usb-peer-fs | デバイス | usb_fs（FS ライブラリ、128 ブロックの疎な RAM ディスク） | device/peer_s3 | FatFS がフォーマットして書いたボリュームを S3（ESP-IDF の FAT）が読み、S3 が書いたファイルを DUT が読む。長い名前のディレクトリとファイル、その場の書き換え。2 往復 |
| usb-peer-host | ホスト | usb_peer_host | host_hid/peer_s3（EspUsbDevice: HID・CDC・MSC） | 列挙と文字列、CDC、キーボード・マウス・コンシューマ、LED、ベンダー HID、MSC |
| usb-peer-host-classes | ホスト | usb_peer_host | host_classes/peer_s3（EspUsbDevice: MIDI・ベンダー・CCID） | MIDI、ベンダーバルクと要求、CCID |
| usb-peer-input | デバイス | usb_input（NKRO キーボード・マウス・ゲームパッド・コンシューマ・システム・カスタム HID、CDC 2 ポート） | device/peer_pico、device/peer_s3 | HID の各レポートのバイト列と解放、2 ポートの識別、ポートごとの行の設定・DTR/RTS のイベント、DTR を落とした間は送らないこと |
| usb-peer-host-input | ホスト | usb_peer_host | host_input/peer_pico のみ（HID 2 つ・CDC 2 ポート） | ゲームパッド、システムコントロール、レポート記述子、HID の生の入力、GET/SET_REPORT と Feature、SET_PROTOCOL、ロック状態とレイアウト、CDC の 2 ポートと行の設定・制御線、相手の抜き差し |
| usb-peer-host-storage | ホスト | usb_peer_host | host_storage/peer_pico のみ（MSC 2 LUN・MIDI・ベンダー・CCID） | MSC の LUN と準備できない媒体（02/3A/00）、MIDI の CC・PC・ピッチベンド・SysEx の往復、ベンダーの要求 OUT、CCID の状態・Escape・電源オフ・カードの抜き差しの通知 |

対向の分担（2026-11-10）。Pico は、アイソクロナス以外の全部を両方の役で受け持ちます。S3 は、別のスタックでの確かめと、
アイソクロナス（usb-peer-media）を受け持ちます。S3 の組の DUT は、ホストを持つ X035（C8T6）に替える予定です（今の F8U6 は
ホストを持たない）。「peer_pico のみ」のケース（host-input・host-storage）の S3 版は、その後に足します。EspUsbDevice は IN
エンドポイントが 4 本までなので、Pico の構成（IN が 6 本・5 本）をそのままは作れず、構成を分けることになります。

S3 側の制約に合わせて試験の形を変えたところがあります。詳しくは下の「ESP32-S3 側の挙動」と、各試験の docstring を
見てください。

ホスト側（usb-peer-host*）は、ホスト機能を持つ DUT が必要です。CH32X035 では R8T6 / C8T6 / G8U6 / G8R6 だけが
ホストを持ち、ベンチの WeAct 板の F8U6 は持ちません（`begin()` が `Unsupported`）。V20x / V30x の USBFS でも動きます。
CH32X035C8T6 と ESP32-S3 で usb-peer-host / usb-peer-host-classes の両方が通っています（デバイス側の usb-peer も
同じ組み合わせで通ります。工場 ISP の VID は 4348 か 1a86 で、試験はどちらも受け付けます。ベンチの F8U6 と、
2026-11-10 の新しい C8T6（UID 0477abcd6c67bc23）は 1a86、それより前の C8T6 は 4348 でした）。
CH32X035C8T6 と RP2040（Pico）でも、usb-peer-media 以外の 6 ケースが通っています（2026-11-10）。

## RP2040（Pico）を相手にする

DUT と同じ OEP プローブに Pico をつなぎ、記録に `usb: {to: rp2040, port: <Pico の口の名前>}` と書きます
（`bench_record.py set <DUT の口> --usb rp2040,port=pico`）。

- **書き込みとコンソール:** Pico への書き込みは SWD で、コンソールは SEGGER RTT（ライブラリ RTT Stream）です。
  どちらも oep-client-python の pytest プラグイン `oep_client.pytest_rp2040_peer` が扱います。run_hardware が
  `-p` で読み込み、`TEST_SERIAL_PORT_PEER_PICO` に Pico の口を渡します。
- **ケースと行の頭:** Pico のスケッチは S3 と同じコマンドを持ち、試験は同じものを使います。答えの行の頭だけ違い、
  host 役は `P `、device 役は `PD ` です（S3 は `S3 ` / `S3D `）。試験は `device_peer.Peer` / `host_peer.Peer` で見分けます。
  - device 役の名前は、S3 と同じ形で別の名前にしています（`Pico peer hid` / `Pico peer classes`、製造元 `ch32rv-tests`）。
  - VID は Adafruit TinyUSB の既定の 239a です。
- **流せないケース:** usb-peer-media は流しません。RP2040 のホストのドライバにアイソクロナス転送がありません。
- **TinyUSB:** Pico 側は、arduino-pico 6.3.1 に同梱の Adafruit TinyUSB 3.7.7 では動きません。
  run_hardware は、3.7.7 の固定のアーカイブに `sketch_support/usb_peer/pico/adafruit-tinyusb-3.7.7.patch` を当てた写しを
  キャッシュに作ります。それを peer の sketch.yaml の `- TINYUSB` の行（`- dir:` に置き換える）で差します。パッチの中身:
  - RP2040 のポートを上流の tinyusb 38bd85a0c0 に差し替える。理由は 2 つあります。
    - control のデータ段が DATA0 で出る不具合（72e09788e4）。
    - 切断のときに EPX の転送が残り、次の列挙で panic する不具合（#4166、#4177）。
  - ホストの列挙のバッファを 1024 にする（usb_contracts の構成記述子は 302 バイト）。
  - `tuh_edpt_xfer()` を有効にする。
  - MSC ホストが転送の失敗で止まらないようにする。失敗した段は失敗の CSW で終え、列挙中は TEST UNIT READY に戻る。

### TinyUSB（Pico）側の挙動

DUT の不具合と見分けるために残します。Adafruit TinyUSB を上げたときは、直っているかを確かめて、回避を外せるか見直します。

- **MSC ホストは転送の結果を見ない。** データ段が STALL でも CSW を読みに行きます。受け取れなかったときは、前のコマンドの
  CSW（成功）のまま完了を返します。CBW のタグも毎回同じです。Pico のスケッチは、読み終えた CSW の署名を消しておき、
  次に CSW が届かなければ失敗とします。
- **MSC ホストは、媒体が準備できるまで TEST UNIT READY を繰り返し、`tuh_mount_cb` を出さない。** Pico のスケッチは、
  ふだんは `tuh_mount_cb` で CONNECTED を出し、構成済み（`tuh_mounted()`）のまま 1 秒たっても来なければ自分で出します。
  構成のすぐ後は、各ドライバが自分の control 要求（GET_MAX_LUN、SET_LINE_CODING）を出しています。その間にスケッチの要求を
  割り込ませると、ドライバの要求が「使用中」で断られて黙って止まりました（MSC が mount されないまま。8 回に 1 回）。
- **`*_sync` の記述子の取得には時間切れが無い。** 途中で装置が去ると、待ち続けることがありました。Pico のスケッチの要求は、
  全部時間切れ付きにしています。
- **`hcd_edpt_clear_stall()` は panic する（上流も同じ）。** STALL した端点を戻せません。STALL の後は、DUT の再起動で
  つなぎ直す順にしています（usb-peer-classes の pull の後）。
- **SdFat 2.3.1（arduino-pico に同梱）は FAT12 を切ってある（`FAT12_SUPPORT 0`）。** DUT の RAM ディスクは FAT12 です。
  そのため Pico のスケッチは、ルートの 8.3 名だけを読み書きする小さな FAT12 / FAT16 を自前で持ちます（`peer_fat.h`）。
- **arduino-pico の device は、`setup()` の前に CDC だけで列挙される。** Pico のスケッチは、インタフェースを足してから
  切って、つなぎ直します。host_classes では、CDC を外して（`clearConfiguration()`）S3 と同じ構成にします。
- **RTTStream の `printf` は 16 進を大文字で書く。** 行は `vsnprintf` で作ります。
- **arduino-pico の Adafruit TinyUSB は、USB の処理（`tud_task()`）を割り込みで回す。** 回すのは `__usb_mutex` が取れたときだけで、
  クラスの API（`tud_midi_*`、`tud_vendor_*`、`Serial`、HID の送信）自身はこれを取りません。`loop()` が FIFO の中にいる間に
  割り込みが来ると、FIFO の lock を 1 秒待ちます（バスリセットのたびに起きる）。この間ホストに答えられず、DUT のホストは
  列挙をあきらめていました。デバイス役のスケッチは、TinyUSB を呼ぶ所を `UsbLock` で囲み、`loop()` の終わりで
  `TinyUSB_Device_Task()` を呼びます。

## 配線と設定

- DUT の USB（D+ / D- / GND）を S3 の USB OTG ポートへ。VBUS は使いません（DUT と S3 はそれぞれ給電）。
- S3 の制御は UART0（ボードの USB シリアル）。ベンチ設定の target に `usb_peer_port`（安定した by-id のパス）を書きます。
- S3 のコアとライブラリは sketch.yaml で版を固定します（esp32 3.3.12、EspUsbHost 2.10.0、EspUsbDevice 3.1.0）。
  run_hardware.py は Arduino の data / downloads を `~/.cache/ch32rv-core-bench/arduino` に置き、実行のたびに
  ESP32 のツールチェーンを取り直さないようにしています（~/.arduino15 には触れません）。

## ESP32-S3 側の挙動

対向試験で見つけた、S3 側（ESP32-S3 本体、EspUsbHost 2.10.0、EspUsbDevice 2.6.0）の変わった動きです。DUT の不具合と
見分けるために残しています。版を上げたときは、直っているかを確かめて、回避を外せるか見直します。

### ESP32-S3 本体

- **書き込みや起動の直後、OTG ポートに内蔵の USB シリアル / JTAG が出る。** S3 は同じ USB PHY を内蔵の
  USB Serial/JTAG（303a:1001、製品名 "USB JTAG/serial debug unit"）と OTG で共有していて、起動時は JTAG 側が
  つながり、スケッチの USB が始まると OTG に切り替わります。DUT がホストのときは、まず 303a:1001 がつながり、
  すぐ切れてから S3 のデバイスがつながるように見えます。試験は目的の VID / PID が出るまで待ちます。
- **UART0（ボードの USB シリアル）を開くと S3 がリセットすることがある。** 自動書き込み用の DTR / RTS が EN と
  BOOT につながっているためで、ポートを開くときの線の状態によっては再起動し、DUT には切断と再接続に見えます。
  手で S3 にコマンドを送るときは、DTR と RTS を落としてから開きます。

### EspUsbHost 2.10.0（S3 がホスト: usb-peer / -classes / -media / -net）

- **ホストのチャネルが 8 本しかない。** usb_contracts の CDC・HID・MIDI を開くと、NCM の分が残りません。
  NCM は usb_net として別のケースにしています。
- **バルクの UVC を読みに来ない。** `videoStart()` は true を返しますが、IN を一度も取りに来ません（payloads=0）。
  アイソクロナスなら届くので、usb-peer-media のカメラはアイソクロナスでビルドします（build_opt.h）。バルクは
  PC ホストの試験で確かめます。
- **アイソクロナスを 2 本同時に流すとパケットを落とす**（マイク入力 + スピーカー出力、マイク + カメラ）。しかも
  `audioInputStop()` が無く、一度始めたマイクを止められません。usb-peer-media はスピーカー → カメラ → マイクの順に
  1 本ずつ流します。
- **マイク入力を毎フレーム取りに来ない。** 32 kHz 16 bit モノラル（1 ms あたり 64 バイト）が約 46 KB/s になります。
  DUT 側は書き込みを拒まれていない（ホストが取りに来た分だけ送っている）ので、試験は連番が途切れないことだけを
  判定し、レートは記録に残します。
- **`audioSend()`（押し込み型）は送れない分を捨てる。** 待たずに受け取り、間に合わない分が欠けます。
  `onAudioOutputRequest()`（引き出し型、足りない分は無音で埋める）なら欠けないので、試験はこちらを使い、合計値の
  一致と送った量以上が届いたことを見ます。
- **`sendHIDVendorOutput()` はデータの先頭バイトをレポート ID として扱う。** CH32RVUsbHost は ID を別の引数で
  付けるので、データの並びが 1 バイトずれて見えます。

### EspUsbDevice 2.6.0（S3 がデバイス: usb-peer-host / -classes）

2026-11-11 に 3.1.0（TinyUSB を使わない版）へ上げ、S3 と P4 の両方で usb-peer-host / -classes が通っています。
下は 2.6.0 で見つけたことです。3.1.0 で変わったかは、項目ごとには確かめていません。


- **IN エンドポイントが 4 本まで。** HID・CDC・MSC・MIDI・ベンダー・CCID を一度に出せないので、S3 のデバイス
  構成を host_hid と host_classes の 2 つに分けています。
- **`sendHidReport()`（マウスの `move()` など）は timeoutMs を使わない。** 前のレポートがまだホストに取られて
  いないと、待たずに即 false を返します。「押す → 離す」を続けて送ると 2 つ目が必ず失敗するので、S3 のスケッチでは
  キーボード・マウス・コンシューマのどのレポートも、取られるまで再試行します（最大 500 ms）。キーボードの `write()` は
  キーの間に 5 ms 待つだけなので、ホストが取りに来るのが遅いと途中で失敗します（実機で一度起きた）。
- **RAM ディスクの `attach()` はメディアを「あり」にしない。** `EspUsbDeviceMscRamDisk` /
  `EspUsbDeviceMscFatRamDisk` の `attach()` は読み書きのコールバックを入れるだけで、`mediaPresent(true)` を呼びません
  （`EspUsbDeviceMscFirmwareDisk` は呼びます）。このままだと TEST UNIT READY が失敗し続け、TinyUSB の MSC ホストが
  上限なく再試行するため、同じデバイスの HID や CDC まで含めてマウントされません。S3 のスケッチで
  `msc.mediaPresent(true)` と `msc.isWritable(true)` を呼んでいます。
- **`keyboard.ledState()` は構造体を返す。** LED のビットは `.leds` です。`%u` でそのまま出すと、Num Lock などの
  フラグも混ざった値（3 のとき 65795）になります。

