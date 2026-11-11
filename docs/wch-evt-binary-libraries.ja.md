# WCH の EVT にある .a（ソースの無いライブラリ）の一覧

WCH の EVT（~/dev_wch/<系列>/EVT/EXAM）で、機能がバイナリ（.a）だけで配られているものの一覧です。2026-11-11 に、
手元の全系列の EVT を調べました（同じ中身のファイルは 1 行にまとめています。中身が同じかは md5 で見ました）。

## ライセンス

**どの .a にも、ライセンスの文書は付いていません。** 付属のヘッダー（.h）の先頭にあるのは、著作権の表示と次の 1 文だけです。

> Copyright (c) Nanjing Qinheng Microelectronics Co., Ltd.
> Attention: This software (modified or not) and binary are used for microcontroller manufactured by Nanjing Qinheng Microelectronics.

- 「WCH のマイコンに使うもの」という用途の限定で、再配布や改変を許すかどうかは書いてありません。
- EVT 全体にも LICENSE の文書はありません。EVT の中で LICENSE を持つのは、同梱の第三者のもの（FatFs、lwIP、LVGL、CoreMark、
  LiteOS）だけです。
- U ディスクのライブラリ（CHRV3UFI）の古いヘッダーは「Copyright (C) W.ch 1999-2019」だけです。
- このコアの方針（[vendor-policy.ja.md](vendor-policy.ja.md)）は「利用条件を確認できない file は取り込みません」なので、今のままでは
  どれもコアに入れられません。使うなら、WCH に再配布の条件を確かめるか、スケッチの利用者が自分で EVT から持ってくる形になります。

## 一覧

| ライブラリ | 中身 | 系列（EVT の場所） | このコアの相当品 |
|---|---|---|---|
| libUHSIF.a | UHSIF（高速並列インターフェース、最大 125 MHz・32 ビット・500 MB/s）の設定と転送。レジスタは RM に載っていない | H417（UHSIF/UHSIF_SLAVE） | CH32RVUhsif（入力・内部クロックだけ。自前の設定） |
| libRV3UFI.a | U ディスク（USB メモリ）のホストとファイルシステム（CHRV3UFI）。FS・HS・SS、ルートハブと外付けハブ | H417、V407、X315、V307、V205、V20x、L103、M030、V103、X035（Udisk_Lib。全系列で同じ中身） | CH32RVUsbHost の MSC（ブロックの読み書き）と FS（FatFs）の組み合わせ |
| libwchnet.a / libwchnet_float.a | TCP/IP のスタック（WCHNET）。V407 の DualNetPorts は 2 ポート（内蔵 MAC と CH390D）用の別版 | H417、V407、V307、V20x（ETH/NetLib） | Network（lwIP 2.2.1） |
| libwchiochub.a | IoCHub（WAN / LAN を自動で切り替える、MCU 同士やホストとの透過の通信） | H417、V407、V307（ETH/IoCHub） | 無い |
| libwchble.a | BLE のスタック（ROM の部分は wchble_rom.hex） | V20x（V208、BLE/LIB） | 無い |
| libMESH.a / libMESHROM.a | BLE Mesh | V20x（BLE/MESH） | 無い |
| LIBWCHLWNS.a | LWNS（BLE の無線を使う、軽い独自の無線ネットワーク） | V20x（BLE/LWNS） | 無い |
| libCH32H417_TOUCH.a、libCH32L103_TOUCH.a、libCH32V00X_TOUCH.a、libCH32V205_TOUCH_CS.a / _CT.a | タッチキー（wchtouch.h） | H417、L103、V00x、V205（TKey / TOUCHKEY） | 無い |
| libCH32_USBPD.a | USB PD のプロトコル（外付けの CH211 を I2C で使う例） | X035、L103（USBPD/USBPD_CH211） | CH32RVUsbPd（チップ内蔵の USBPD を自前で駆動） |
| libPD_CH32M030.a | USB PD（M030 の PD 応用） | M030（APPLICATION/PD） | CH32RVUsbPd（M030 の 2 つ目のブロックは未対応） |
| libMOTOR_1V1-1.a | 名前はモーターだが、中身は USB PD（PD_Init などの関数） | L103（APPLICATION/PD_Electric_Fan） | — |
| libM12013_SV_*_LIB.a、libM12014_SV_00x_*.a、libM12023_SV_*_COMCoreV1.5_Library.a、libvoilent_fan_SRC-Lib.a、libbldc.a | モーター制御（センサーレスの FOC、SVPWM、逆起電力の推定など）。扇風機、ドライヤー、BLDC の応用例 | L103、M030、V006、V20x（APPLICATION） | 無い |
| IQmath_RV32.a、libIQmath_RV32.a、libIQMath_RV32EC_ZMMUL_XW.a | 固定小数点の数学（TI の IQmath に相当）。V006 用は RV32EC + Zmmul 版 | V006、L103、M030、V20x（モーター制御の例に同梱） | 無い |
| libVoiceRcg.a | 音声認識（MFCC の特徴量と照合） | V307（VoiceRcgExam） | 無い |
| libwui_0.2.a、libwui_v006_0.2.a | WUI（小さな LCD 向けの GUI。中の関数名は難読化されている） | L103、V205、V20x、V006（APPLICATION/WUI） | 無い |

- 同じ名前でも中身の違うものがあります: libCH32_USBPD.a は X035 と L103 で別物、libIQmath_RV32.a は M030 だけ別物、
  libwchnet.a は V407 の DualNetPorts だけ別物です。
- 手元に EVT の無い系列（V003 系など）は調べていません。

## このコアから見た候補

- **UHSIF:** H417 で取り込みを速くするには、これが要ります（WeAct の LogicAnalyzerV2 は、入力 16 本を UHSIF の
  PORT16〜31 につないで、200 MHz × 16 ch を取っています）。レジスタは非公開ですが、FX3 の GPIF II と同じ作りと分かり、
  ライブラリを使わずに取り込めるようにしました（CH32RVUhsif、[h417-uhsif.ja.md](h417-uhsif.ja.md)）。
- **タッチキー:** 自前で書くには、各系列の TKEY のレジスタが RM にあるかを先に確かめます。
- **BLE（V208）:** 無線の部分の資料が無いので、自前では難しいと思われます。
