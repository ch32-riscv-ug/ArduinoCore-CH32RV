# ListProfiles

English: [README.md](README.md)

PD 充電器の PDO を一覧表示し、9 V を要求します。読み出し専用の例ではなく、VBUS 電圧が実際に変わります。

## 必要なものと配線

CH32X035 などの USBPD のある系列(CC の端子はライブラリの README) の PD CC 配線、PD 充電器と USB-C ケーブル、UART モニタ。VBUS を受けるボード電源回路・負荷は要求する9 Vに対応させます。

## 操作と期待する結果

1. UART を115200 bpsで開き、PD 充電器を接続して起動します。
2. `the charger offers:` 以下の kind、電圧範囲、電流を確認します。
3. 要求が成立すると `now at 9000 mV...` と契約番号が出ます。
4. 9 V が選べない/要求が失敗した場合は `this charger has no 9 V` が出ます。この表示は拒否・交渉失敗も含む簡易メッセージです。

## 設定と制約

初回の契約待ちは最大約5秒です。Type-C だけで PD 非対応の電源は connected でも ready になりません。`request(9000)` は固定9 Vを優先し、対応 PPS も候補になります。表示する電圧・電流は測定値ではなく契約値です。

## API の使い方と改造のポイント

| API | この例での使い方 |
|---|---|
| `USBPD.begin()` | Sink として CC の接続検出を開始します。成功直後に明示契約があるとは限りません。 |
| `connected()` / `ready()` | Type-C の接続と、PD の明示契約成立を区別します。これらの呼び出しもドライバを進めます。 |
| `profileCount()` / `profile(i)` | 相手の広告 PDO を0始まりの index で読みます。`kind` と各フィールドの単位を確認します。 |
| `request(millivolts, milliamps)` | 電圧を要求して結果を待ち、成功を `bool` で返します。電流の省略値は広告に応じた要求です。 |
| `contractProfile()` / `voltage()` / `current()` | 成立した契約を確認します。相手が広告した最大値と、選んだ契約を区別します。 |

`request(9000)` を `request(9)` にすると9 mVの意味になります。固定 PDO は指定電圧に一致するものを選び、
PPS は広告範囲内の電圧を選びます。特定の PPS PDO を明示したいときは `requestProfile(index, mv, ma)` を使います。

この例は起動時に一度だけ一覧表示と要求を行います。あとから充電器を接続・交換して同じ表示をしたいなら、
`loop()` で接続・契約状態の変化を見てやり直します。失敗表示は簡易なものなので、能力一覧だけで交渉失敗の原因を断定しません。

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbPd の API と対応範囲](../../README.ja.md)
- [USB 全体のガイド](../../../../docs/usb.ja.md)

[ListProfiles.ino](ListProfiles.ino)
