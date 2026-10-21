# CH32H417 評価ボードでの書き込みと復旧

CH32H417 の評価ボードで、USB3.0-A(SS)を PC に挿したまま実験を回すための手順と注意です。2026-10-20〜21 の実機の作業で分かったことをまとめています。
道具は [tools/diagnostics/h417-bench](../tools/diagnostics/h417-bench/README.ja.md) にあります。

## ベンチの構成

| 線 | つながっている先 | 備考 |
|---|---|---|
| SWD(PB8 / PB9)と NRST | WCH-LinkE(シリアル 49808F06CE30) | LinkE の RST を NRST に配線してある |
| UART(PB6 TX / PB7 RX) | 同じ LinkE の UART(PC からは `/dev/ttyACM8`) | VDDIO の端子。スケッチでは `Serial.begin(115200, SERIAL_8N1, PB7, PB6)` |
| USBFS(PA11 / PA12) | PC | |
| USB3.0-A(SS と USB 2.0 の線) | PC | **USB 2.0 の D+ / D− は PB8 / PB9、つまり SWD と同じ線** |

- 評価ボードは、USB3.0-A の D+ / D− とデバッグ用のヘッダ(P3)が同じ PB8 / PB9 に直結されています。
  - USB3.0-A を挿していると、PC がその線で列挙を試み(カーネルのログでは `usb 3-3`、low-speed で error -71 が続く)、その間は SWD が乱れます。
  - デバッグのピンを別のピンへ移す設定はありません。
- PC の WCH-LinkE は、ハブ 3-2.2 のポート 1 にいます(`uhubctl -l 3-2.2`)。同じハブのポート 2〜4 には別の WCH-Link がいるので、触るのはポート 1 だけにします。
- 共有ロック `/home/mt/dev_oep/oep-bench/locks/bench.lock` を `flock` で取ってから操作します。ほかのセッションと台を分け合っています。

## 書き込みの手段

| 手段 | SS を挿したまま | 条件 |
|---|---|---|
| DFU(FS または SS) | 使える | 動いているアプリに DFU runtime(既定で付く)があり、USB が生きていること。`dfu.py`、または `ch32rv boot dfu flash` |
| SWD(`ch32rv flash`) | **使えない** | SS を挿していると、フラッシュの書き込みや長い読み出しが途中で `reason 0x55` になる(`--speed low` でも同じ)。短い操作(halt、レジスタや RAM の読み書き、`reset`)は通ることが多い |
| SWD(SS を抜いた状態) | — | 確実に使える。止まったイメージからの最終的な戻り先 |
| 工場出荷の USB ISP | 使えない | 電源投入時に DP(PA12)が High のときだけ入る。ソフトの BOOT_MODE + リセットでは入らない |
| 工場出荷の UART ISP | 使えない | 有効にしたうえで電源を入れ直す必要がある |

**SS を挿したまま回す前提は、「アプリが生きていて、DFU の入口が残っていること」です。** 入口のないイメージ(USB を使わないスケッチ)や、起動直後に止まるイメージを書くと、SS を抜くまで戻せなくなります。

## 実験用スケッチの決まり

1. **`UsbFallback.h` を入れる。**
   - SS で 5 秒 configured にならなければ FS に移り、FS でもだめなら SS に戻ります。
   - UART と CDC で、`p`(状態を返す)、`f`(FS へ)、`s`(SS へ)を受けます。どれも 1 文字だけの行で送ります。
   - 状態の判定に「`millis() | 1` を設定済みの印」にする書き方は使わないこと(1 ms 未来の値になり、時間切れがすぐ起きる)。
2. **USB を使わないスケッチは書かない。** 試験用のコピーには、`CH32RVUsbDevice` と CDC を足して `begin()` しておきます。DFU runtime は既定で付きます。
3. **初めて動かす周辺回路のコードには、止まらない作りを入れる。**
   - 待ちのループには必ず時間切れを付けます。
   - DMA は、書き込みの範囲が有界な方式にします。HSADC のピンポンなしの DMA は、DMA_LEN を越えて書き続けて、ほかの変数を壊しました。
4. **USBHS / USBSS / OTG のリセットとクロックには触らない。** 特に halt 中は避けます(下の「やってはいけないこと」)。
5. **VIO18 の端子(PB0〜1、PB10〜14、PC0〜3、PC6〜12、PD0〜7、PD9〜15、PE0〜1、PE7〜15、PF0〜5、PF11〜14)に注意する。**
   - 評価ボードの VIO18 は 1.2 V で起動し、入力は VIO18 + 0.3 V までです。
   - 3.3 V の信号を扱うときは、メニューの VIO18 Supply か `CH32RV.setVio18Millivolts(3300)` で 3.3 V にします。
6. QingKe V3F では `csrr mcycle` が不正命令になります。時間は `micros()` で測ります。

## 復旧の手順(軽いものから順に)

機材に触れてもらう前に、上から順に試します。

1. **UART のコマンド:** `python3 uart.py /dev/ttyACM8 p` で生きているかを見て、`f` / `s` で FS / SS を切り替えます。
2. **ホストから configuration を 0 にする:** SS で configured のまま FS に移したいとき、`SET_CONFIGURATION(0)` を送ります(pyusb の `ctrl_transfer(0x00, 9, 0, 0)`)。5 秒後に、UsbFallback が FS へ移ります。
3. **DFU:** `dfu.py <bin>`。
   - SS の構成記述子が壊れていて DFU のインターフェースが見えないときは、番号を指定して DETACH を送ります(`dfu.py <bin> 3`)。
   - ただし、libusb は、パースできた構成にないインターフェースへの要求を `Entity not found` で断ります。
4. **NRST のリセット:** `ch32rv --probe serial:49808F06CE30 --chip CH32H417 --connect-under-reset reset`。
   - SS を挿したままでも、たいてい通ります。
   - NRST を 20 ms 下げて接続し直すだけで、フラッシュにもオプションバイトにも触りません。
5. **RAM 上で落ちたアプリを、書き換えて起動し直す**(`ram_restart.sh <elf> <関数名>`)。
   1. `dbg regs` で pc を見て、`riscv-none-elf-addr2line -f -C -e <elf>` で場所を調べます。止まっている先が既定の割り込みハンドラ(どの名前でも同じ無限ループ)なら、例外か処理されない割り込みです。
   2. `dbg reg read csr:834`(mcause)、`csr:833`(mepc)で、原因と場所を読みます。数字は 10 進で書きます(0x342 = 834)。
   3. 原因の関数の入口を `c.li a0,0; c.jr ra` に書き換えます。次に `x2`(sp)を `_eusrstack` に、pc を `ch32rv_loadcode_ram` にして再開します。
   4. イメージの複写を飛ばして .bss の初期化から起動し直すので、RAM の書き換えは残ります。
   5. **必ず M モードで止めること**(例外のハンドラの中)。`loop()` の中(U モード)で止めて同じことをすると、`csrw 0xbc0` が不正命令になります。
   6. 単純な待ちループで止まっているだけなら、その分岐を `c.nop`(0x0001)に書き換えて再開するだけで抜けられます。
   7. 起動し直したアプリが USB を使えるようになれば、DFU で正しいイメージに戻せます。前の実行の USB の状態が残って USB が立ち上がらないことがありますが、**その対策に RCC のリセットのビットを使ってはいけません**(下の「やってはいけないこと」)。
6. **WCH-LinkE が固まったとき:**
   - 症状は、どの命令も `transfer timed out` / `endpoint stalled` になり、LinkE が USB から消えたり出たりすることです。
   - fw 2.22 は、応答のないターゲットへの接続に何度か失敗すると固まります。USB のリセットでは戻りません。
   - `uhubctl -l 3-2.2 -p 1 -a cycle -d 2` で電源を入れ直し、ほかの接続を挟まずに `--connect-under-reset target info` を 1 回だけ実行します。失敗が続くときは、何度も試さず、電源の入れ直しからやり直します。
   - LinkE が出直すたびに、udev から `board-identify@ttyACM8.service`(root)が起動して、LinkE のインターフェースを取り、ターゲットに接続しにいきます。それで「interface is busy」になったり、固まったターゲットを叩いて LinkE を固めたりします。
   - 復旧の間は止めてもらいます: `sudo systemctl mask board-identify@ttyACM8.service`、`sudo systemctl stop board-identify@ttyACM8.service`。済んだら `unmask` します。
7. **PC の列挙を止める:** USB3.0-A の USB 2.0 側の列挙が SWD を邪魔するときは、`echo 1 | sudo tee /sys/bus/usb/devices/usb3/3-0:1.0/usb3-port3/disable` で止めます。済んだら `echo 0` で戻します。root が要ります。
8. **最後の手段:** H417 の電源の入れ直し(USB のケーブルを全部抜き差し)。挿し直すときは USB3.0-A を抜いたままにして、SWD で書き込める状態にしてから、SS を挿します。

## やってはいけないこと

- **`ch32rv recover` を H417 に使わない。** ch32rv 0.22.0 からは、H417 では ch32rv 自身が断ります。
  - 中身は LinkE の独自の消去(命令 0x0d 0x08 / 0x0f)ですが、H417 には手順が合っていません。
  - コードのフラッシュに消え残りが出たうえ、オプションバイトが USER = 0x3F、DATA と WRP が消去済み(0xe339)に書き換わりました。
  - オプションバイトは `target option reset` で工場出荷の値に戻せます(SS を抜いた状態で)。
- **halt 中に RCC_HBRSTR(0x4002102C)の USB のビットを書かない。**
  - ビットは、1 を書いている間ずっとリセットを保ちます。USBHS(ビット 11)、USBSS(ビット 12)、OTG(ビット 17)が対象です。
  - 2026-10-21、halt 中に 0x00021800 を書いた直後から SWD が応答しなくなり、そのあと NRST での接続を試すと、LinkE が RST を駆動するたびに落ちるようになりました。ソフトでは戻せず、電源の入れ直しが必要になりました。
- 応答のないターゲットへの接続を何度も繰り返さない(LinkE が固まります)。
- SS を挿したまま `ch32rv flash` を当てにしない(途中で失敗し、消去だけ通ることがあります)。

## 2026-10-21 の時点で残っていること

- H417 は、USB にも UART にも SWD にも応答しない状態です(上の HBRSTR の件)。
- LinkE の電源を入れ直すと LinkE は戻りますが、RstOut(NRST を下げる命令)を送るたびに LinkE が落ちます。普通の接続もタイムアウトします。
- H417 の側がリセットを出し続けている(NRST は双方向の端子)か、電源の状態がおかしいと考えていますが、測れていません。
- 次に機材に触れられるときに、電源を入れ直して確かめます。
