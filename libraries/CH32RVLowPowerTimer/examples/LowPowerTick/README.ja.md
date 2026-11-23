# LowPowerTick

[English](README.md)

`lightSleep()`（Stop）で眠り、低消費電力タイマー（LPTIM）に起こしてもらいます。LPTIM は LSI で数えるので、ほかのクロックが止まっている間も動きます。

## 配線

要りません。

## 手順と期待する結果

書き込むと `LPTIM at 1250 Hz` のように表示し（LSI の公称値から。実際の LSI は数十 % ずれます）、そのあと 1 秒に約 4 回 `woke, tick 1`、`woke, tick 2` … と表示します。

## うまくいかないとき

- `LPTIM begin() failed`: その系列に LPTIM がありません（CH32L103 / M103、CH32H41x にあります）。
- 間隔が 250 ms から大きくずれる: LSI の誤差です。正確な間隔が要るなら `CLOCK_LSE`（RTC Clock メニューで LSE を選び、水晶がある板）を使ってください。

## 実行前の準備

出力はハードウェア `Serial` の 115200 baud・8N1 です。生成された `sketch.yaml` にビルド profile があります。[ビルド規則](../../../../docs/examples-build-rules.ja.md)と[書き込み経路](../../../../docs/upload-routes.ja.md)も参照してください。
