# ListProfiles

English: [README.md](README.md)

PD 充電器の PDO を一覧表示し、9 V を要求します。読み出し専用の例ではなく、VBUS 電圧が実際に変わります。

## 必要なものと配線

CH32X035/X033 の PD CC 配線、PD 充電器と USB-C ケーブル、UART モニタ。VBUS を受けるボード電源回路・負荷は要求する9 Vに対応させます。

## 操作と期待する結果

1. UART を115200 bpsで開き、PD 充電器を接続して起動します。
2. `the charger offers:` 以下の kind、電圧範囲、電流を確認します。
3. 要求が成立すると `now at 9000 mV...` と契約番号が出ます。
4. 9 V が選べない/要求が失敗した場合は `this charger has no 9 V` が出ます。この表示は拒否・交渉失敗も含む簡易メッセージです。

## 設定と制約

初回の契約待ちは最大約5秒です。Type-C だけで PD 非対応の電源は connected でも ready になりません。`request(9000)` は固定9 Vを優先し、対応 PPS も候補になります。表示する電圧・電流は測定値ではなく契約値です。

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbPd の API と対応範囲](../../README.ja.md)
- [USB 全体のガイド](../../../../docs/usb.ja.md)

[ListProfiles.ino](ListProfiles.ino)
