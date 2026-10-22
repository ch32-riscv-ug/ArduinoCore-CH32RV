# PowerSource

English: [README.md](README.md)

PD Source の protocol と、VBUS スイッチ・可変電源を接続する callback の骨格です。そのままでは実電源を制御しません。

## 必要なものと配線

CH32X035 などの USBPD のある系列(CC の端子はライブラリの README) の Source 用 CC 回路、電流制限付き電源、VBUS スイッチ、可変レギュレータ、UART モニタ。WeAct X035 の sink用 Rd は R4/R5で、Source 化には除去が必要です。

## 操作と期待する結果

1. `vbus()` と `setVoltage()` を実際の電源回路制御に置き換えます。callback は切替完了時だけ true を返すようにします。
2. 広告する 5 V/3 A、9 V/2 A、PPS 3.3～11 V/2 A を、回路が実際に供給できる値へ変更します。
3. sink を接続し、`VBUS on (5 V)`、要求に応じた固定/PPS値、`contract: profile ...` を観察します。
4. 抜去時の VBUS off と、reset/失敗時の5 V復帰を電源回路を含めて確認します。

## 設定と制約

**付属 callback は print 後に常に true を返します。** そのまま実 sink と試すと、実電圧を変更していないのに PS_RDY を送るため、まず電源制御を実装します。`setRpCurrent()` も電源を3 A化する API ではありません。Sink の `USBPD` と Source の `USBPDSource` は同時使用できません。

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbPd の API と対応範囲](../../README.ja.md)
- [USB 全体のガイド](../../../../docs/usb.ja.md)

[PowerSource.ino](PowerSource.ino)
