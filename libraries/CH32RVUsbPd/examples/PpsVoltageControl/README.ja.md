# PpsVoltageControl

English: [README.md](README.md)

USB PD 充電器の PPS（電圧を 20 mV 刻みで選べる範囲）の電圧を、UART から mV で決めます。PPS の無い充電器、範囲の外の電圧、
断られたとき、抜いたときを扱います。

## 必要なものと配線

CH32X035 などの USBPD のある系列（CC の端子はライブラリの README）、PPS に対応した PD 充電器、UART モニタ。
**電圧が実際に変わります**: 基板の VBUS の入力と負荷が、充電器の PPS の範囲の最大の電圧に耐えることを先に確かめます。
求める電流は `REQUEST_MA`（既定 1000 mA）です。

## 操作と期待する結果

1. 充電器をつなぐと、契約の電圧と PPS の範囲が出ます。

   ```
   charger ready at 5000 mV
   PPS profile 4: 3300..11000 mV, up to 3000 mA
   type a voltage in mV and Enter
   ```

2. `7400` と打って Enter: `contract: 7400 mV, up to 1000 mA (PPS)`。外の電圧計で VBUS を確かめます。
3. 範囲の外（`20000` など）は `20000 mV is outside 3300..11000 mV`。充電器が断ると `refused; still ... mV`。
4. PPS の無い充電器では `this charger has no PPS range (fixed voltages only)`。抜くと `charger unplugged`。

## API と処理の流れ

| API | この例での使い方 |
|---|---|
| `ready()` / `connected()` | 契約の有無と、抜いたか交渉中かを分けます。 |
| `profileCount()` / `profile(i).kind` | 広告から `CH32RVUsbPdSupply::Pps` の範囲を探し、`min_mv` / `max_mv` を見ます。 |
| `requestProfile(index, mV, mA)` | その範囲の電圧を求めます。答えを待つ呼び出し（最大約 1.5 秒）で、断られると false で前の契約のままです。 |
| `voltage()` / `current()` | 受け入れられた契約の値です。測った値ではありません。 |

PPS の契約は、充電器が切らないように、ドライバが 5 秒ごとに求め直します（`delay()` と `loop()` の戻りで動く）。
