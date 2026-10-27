# コアが使っていない周辺機能(調査の記録)

2026-10-22 に、RM、DS、EVT、ch32-device-data を読んで調べた。用途は限定していない。パラレル入力(多くの GPIO を一度に取り込むこと)に使えそうなものを先に詳しく見た。**どれも採用は決めていない。**

- 実機で確かめたのは、H417 の Ethernet のリンクだけ(末尾)。
- 「推定」と書いた値は、文書の数値から計算したもので、測っていない。
- 速さは各系列の最大のクロックでの値。今のコアのクロックでは比例して下がる(L103 は 8 MHz、M030 は 72 MHz、X315 は 20 MHz、V00x は 24 MHz、H417 の HCLK は 100 MHz)。

## 調べて分かった、今の使い方の事実

- タイマーは PWM と tone だけ。入力捕捉はない(`ch32rv_timer.c`)。対応表の TIM の行の「入力捕捉」は実装と合っていない。`pulseIn` はソフトで数えている。
- DMA は USB のドライバの中だけ。TIM と DMA でポートを取り込む `CH32RVLogicCapture` と `CH32RVHsAdc` は H41x だけ。
- ADC は単発の analogRead だけ。DAC は値を出すだけ。
- 対応表の H417 の列は古い。RNG(TLS 用)と FLASH の自己書き換え(DFU 用)は、内部で使っている。
- V407 と V467 は、ベクトル拡張(Zve64x、RVV 1.0)を持つが、`rv32imac_zicsr` でビルドしていて使っていない。

## 1. パラレル入力の候補

### H417

| 候補 | 幅 | 外部クロック | 内部クロック | 連続 | 上限(文書・推定) | 弱いところ |
|---|---|---|---|---|---|---|
| UHSIF | 8/16/24/32 bit | slave で 125 MHz 以下 | master で SYSCLK を分周(例 100 MHz) | 4 ライン × 8 バッファの輪番 | 公称 500 MB/s | レジスタが公開されていない(EVT は閉じたライブラリ `libUHSIF.a`)。ピンは全部 VIO18 |
| DVP | 8/10/12 bit | PCLK 150 MHz(DS)/ 96 MHz(RM 本文、食い違う) | TIM の PWM を PCLK に配線すれば | 専用 DMA が BUF0/BUF1 を交互に | 推定 12 bit で 50 MS/s 以上 | HSYNC/VSYNC の扱いと行の継ぎ目が文書にない。FIFO は 4 B |
| FMC の読み出し | 8/16/32 bit | NWAIT で待たせるだけ | 非同期の読み出しサイクル | DMA のメモリ間転送だけ(循環できない) | 推定 16 bit で 20〜33 MS/s | 間隔が揺れる。外付けの回路が要ることがある |
| QSPI の dual-flash 読み出し | 8 bit(2 個で 16 bit) | できない(SCK は出力) | SCK = HCLK/(N+1)、75 MHz 以下(DS) | 長さ無制限で読み続けられる | 推定 8 bit で 50 MS/s | FIFO が満杯だと SCK が止まり、時間軸に穴が開く(検出できない) |
| TIM + DMA で GPIO(今の方式) | 16 bit | ETR、CHx の捕捉イベントで | TIM6 | 動いている | 実測 1 本で 12.5 MS/s、2 本で 20 MS/s | — |
| V5F の読み取り | 16/32 bit | — | コアのクロック | 動いた | 実測 20 / 25 MS/s(docs/h417-multicore.ja.md) | USB の DMA に押されて揺れる |
| SPI ×4 の slave 受信 | 1 bit/個 | 75 MHz 以下(DS) | TIM の出力を SCK に配線 | DMA の循環 / ダブルバッファ | 1 本あたり推定 4.7M 半語/s | 4 本の語の境目がそろわない |

パラレル入力に使えないもの: SDMMC/SDIO(SD の枠組みが前提)、ETH(フレームが前提)、DFSDM(並列入力の元は CPU か DMA の書き込み)、PIOC(ピン 2 本)。

### ほかの系列

| 候補 | 系列 | 幅 | クロック | 上限(文書・推定) | 弱いところ |
|---|---|---|---|---|---|
| DVP | V407/V467(KEU6 以外)、V307VC/V317VC(100 ピン) | 8/10/12 bit | 外部 PCLK(V407 150 MHz、V30x 96 MHz) | 短い時間なら 8 bit で 100 MS/s 前後もありうる(バスは未確認)。流し続けるなら USBHS で約 40 MB/s | USART1(PA9/10)、SDIO と同じピン。ピンは 4 ポートに散らばる |
| TIM + DMA で GPIO | X315/X305 | 24 bit | 内部(TIM)、外部(ETR/TIx) | 313 MHz ÷ 8 ≒ 39 MS/s(推定)。DMA のダブルバッファあり、USBSS あり | 転送のクロック数を測っていない |
| TIM + DMA で GPIO | 全系列 | 8 bit(V003/V00x)、24 bit(X035)、ほかは 16 bit | 内部(TIM の update)、外部(ETR は TIMxCLK/4 以下) | HCLK ÷ 8: V30x で約 18 MS/s、V205 で 24、V407 で 25、V003 で 6(推定) | EVT に DMA で GPIO を読む例はない |
| QSPI の dual-flash 読み出し | V205、V203CCT6 | 8 bit | 内部 SCK(HCLK 直結まで) | 未確認 | H417 と同じ(FIFO が満杯だと SCK が止まる) |
| FSMC の読み出し | V303VC、V307VC、V205VCT6、V407/V467 VET6 | 16 bit | 内部(NOE)、NWAIT | 144 MHz で約 20 MS/s(推定) | 100 ピンだけ、アドレスとデータの多重化だけ。外付けのラッチか FIFO が要る |
| タイマーの捕捉でエッジの時刻とレベルを記録 | V003、V00x、X035、V205、L103、X315、M030(CAPLVL)。M030 は両エッジの捕捉 | 4 チャネル | 内部 | エッジの数しだい | 変化の少ない信号向き。V103、V20x、V30x、V407 には CAPLVL がない |
| SDIO の 8 bit 受信、PIOC | — | — | — | — | パラレル入力には向かない |

## 2. そのほかの未使用の機能

### 通信と外部バス

| 機能 | 系列 | 要点 |
|---|---|---|
| Ethernet(MAC と内蔵 10/100M PHY) | H417(QEU6、MEU6、WEU6)、V317、V407/V467 | MDIX、PTP。H417 はギガビットの MAC(RGMII は外付け PHY)。EVT のプロトコルスタック(WCHNET)は閉じたライブラリで、MAC_RAW の例とドライバはソース |
| Ethernet(内蔵 10M PHY) | V208、V307(V307 はギガビットの MAC も) | — |
| SerDes | H417 | 1.5 Gbps の差動リンク、8b/10b、絶縁できる。ケーブル(Cat6)で 5 m なら 1.5 Gb/s、100 m なら 0.5 Gb/s(文書) |
| CAN 2.0B | H417(3 個、TTCAN)、V20x、V205、V30x、V407、L103 | V20x は CAN1 と USBD が同時に使えない |
| CAN FD | L103(C8T6、K8U6)だけ | 8 Mbit/s まで。H417 の CAN に FD はない(ビットが予約) |
| I3C | H417、V407/V467 | controller と target、IBI、Hot-Join、DMA |
| I2S、SAI | I2S は H417、V30x、V407。SAI(TDM、SPDIF 出力)は H417 | — |
| SDMMC(host と SDIO slave、HS200/HS400) | H417 | 片エッジで 200 MHz(DS) |
| SDIO host | H417(QEU6)、V30x、V407 | コアの SD は SPI 接続 |
| FMC / FSMC | H417(SDRAM も 256 MB まで)、V30x、V205、V407 | 外部 SDRAM を深い取り込み用のメモリにもできる。パラレル LCD |
| QSPI | H417(QEU6 は 2 個)、V205 | Flash や PSRAM、メモリマップ |
| LTDC | H417、V407/V467 | RGB888 の出力。24 bit のパターン発生器にもなりうる(推定) |
| PSRAM(チップ内 4〜8 MB、600 MB/s) | V467 だけ | 専用 DMA、アドレスでそのまま読める。MCU では珍しい |
| GPHA(2D の DMA) | H417 | 塗りつぶし、コピー、形式変換 |
| SWPMI | H417 | NFC/UICC の SWP master |
| ARGB(WS2812 型 LED) | V407/V467、X305/X315 | DMA あり |
| BLE 5.3 | V208 だけ | 閉じたライブラリ |
| USB | USBHS の host(全系列)、USB PD(V205、L103、M030、X305/X315。コアは X035 だけ) | — |

### アナログ

| 機能 | 系列 | 要点 |
|---|---|---|
| ADC の DMA、デュアル(インタリーブ)、注入、ウォッチドッグ、TIM トリガー | ほぼ全系列(デュアルは H417、V20x、V30x、V407) | H417 は入力 PGA ×1/4/16/64 もある |
| ADC 4 個(各 5 Msps)、外付けの CH448 で 384 チャネル | X305/X315 | MCU では珍しい |
| DAC の DMA 波形、三角波とノイズ | H417、V30x、V407 | 任意波形 |
| DFSDM | H417 | ΣΔ の絶縁計測、PDM マイク |
| OPA / PGA / CMP | 多くの系列 | CMP の出力は TIM の捕捉やブレーキにつながる。取り込みの閾値を変えられる入力段にもなる(推定) |
| M030 の QII、ISP、ISINK、ISOURCE | M030 | 電流検出、外付け DC-DC の調整(PPS) |
| TouchKey | V00x、V103、V20x、V205、V30x、L103、X035、H417 | — |

### タイマー(全系列にあり、コアは PWM しか使っていない)

- 入力捕捉、PWM 入力、エンコーダ、ワンパルス、ETR の外部クロック、タイマー同士の連結、ADC/DAC の起動、相補出力とデッドタイム、ブレーキ、DMA バースト。
- 珍しいもの: CAPLVL(捕捉したときのレベルも記録)、M030 の非対称なデッドタイム、32 bit の TIM4(V205、X315、L103)、L103 の LPTIM(Standby 以外で動く)。

### システム

| 機能 | 系列 | 要点 |
|---|---|---|
| V5F(2 つ目のコア)、IPC、HSEM | H417 | docs/h417-multicore.ja.md |
| PWR の各モード | 全系列 | H417 には Standby がない。Stop でも 1.8〜4.4 mA |
| RTC / BKP | 多くの系列 | H417 の RTC はカレンダーがなく、BKP もない |
| WWDG、CRC | 多くの系列 | CRC は CRC-32 固定 |
| ECDC(AES、SM4) | H417 | ECB と CTR だけ |
| フラッシュの自己書き換え | H41x 以外 | docs/persistence-spec.ja.md(下書き) |
| ベクトル拡張(RVV 1.0) | V407/V467 | 使っていない。ビットの並べ替えや圧縮に使える(推定) |
| 高耐圧の電源とゲートドライバ | M030(5〜28 V、ハーフブリッジ 4 組)、M007(6〜48 V、3 相) | コアは扱っていない |

## 3. 分からないことと食い違い

- UHSIF: レジスタが公開されていない。取り込みに使う形(CS# と WR# を固定して外部のクロックを入れ続ける)は、マニュアルの動作の外。
- DVP: H417 の PCLK の上限が、DS(150 MHz)と RM 本文(96 MHz)で食い違う。行の継ぎ目と、HSYNC がいらない形(JPEG モード)の挙動が書かれていない。
- 専用の DMA(DVP、UHSIF、SerDes、SDMMC)が SRAM へ書く速さと、バンクの取り合いが書かれていない。USB と DMA の取り合いは、H417 で実測済み(docs/h417-capture.ja.md、docs/h417-multicore.ja.md)。
- H417 以外で、DMA が GPIO を 1 回読むのにかかるクロック数は測っていない。上の MS/s は、H417 の 8 HCLK を当てはめた推定。
- H417 の高速なピンの多くは VIO18 にある。今の取り込みで使っている PE7〜15 は、UHSIF、FMC、QSPI、SDIO のピンと重なる。
- DS の表は、pdftotext で読むと列がずれることがある。DVP、FSMC、V208 の ETH のパッケージごとの有無は、PDF の表で確かめ直したほうがよい。

## 4. 実機で確かめたこと: H417 の Ethernet のリンク(2026-10-22)

評価ボードの RJ45 をスイッチにつないで確かめた。相手はベンチの PC ではない(PC の空いている NIC はリンクしていなかった)。

- 手順: HSE(25 MHz)→ ETH PLL(RCC CTLR の bit 26、準備完了は bit 27)→ MAC のクロック(HBPCENR の bit 14)→ PHY の電源断とリセットを解く(MACPHYCR の bit 30 を 0、bit 31 を 1)→ MDIO で PHY(アドレス 1)を読む。EVT の `eth_driver_100M.c` と同じ順。
- 結果:
  - PHY ID は 0x7371 / 0x9011。
  - BSR 0x786D(リンクあり、自動ネゴシエーション完了)。電源を入れて 1 秒後に上がった。
  - 相手の能力は 0xC5E1(100/10 の全二重と半二重、ポーズフレーム)。
  - 100 Mbps 全二重でつながった。
- フレームの送受信(MAC の DMA の記述子、ARP や DHCP)は、まだ試していない。
