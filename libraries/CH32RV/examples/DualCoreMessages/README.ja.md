# DualCoreMessages

[English](README.md)

CH32H417 の 2 つのコアがやり取りします。V5F が V3F に数を送り（IPC）、V3F が素数かを調べて答えを返し、2 つのコアで共有する合計をハードウェアのセマフォ（HSEM）で守ります。

## 機材と接続

**CPU Core = V5F**（既定）の CH32H417 のボード。

- `Serial`（V5F）に UART のモニターをつなぎます（CH32H417QEU6 では PB14 が TX、PB15 が RX）。
- 任意: `Serial1`（V3F）に 2 つ目のモニター（CH32H417QEU6 では PD5 が TX、PD6 が RX）。PD5 / PD6 は VIO18 の端子なので、3.3 V のモニターなら VIO18 Supply のメニューを 3.3 V にします。

## 手順と期待する結果

- `Serial` に 0.5 秒ごとに `V5F: 1000003 is prime (1 of 1 tested are prime)` のような行が出ます。数は 2 ずつ増え、調べた数と素数の数も増えます。
- `Serial1` には V3F が `V3F: 1000003 is prime` のような行を出します。
- V3F から答えが 1 秒来なければ `V5F: no answer from the V3F`、V5F が前の答えを 100 ms 取らなければ V3F が `Serial1` に `V3F: the V5F did not take the last answer` を出します。

## API と処理の流れ

- `CH32RV.ipcOnReceive(cb)`: このコアに届いたメッセージで `cb(uint32_t)` が割り込みで動きます。`setup1()` で呼ぶと V3F の割り込み、`setup()` で呼ぶと V5F の割り込みです。
- `CH32RV.ipcSend(msg, ms)`: 相手のコアへ 32 ビットを 1 つ送ります。前のメッセージが取られるまで最大 `ms` 待ち、入れば true。
- `CH32RV.hsemTake(ch)` / `CH32RV.hsemRelease(ch)`: HSEM のチャネルを取って離します。取ったコアだけが離せます。間に読み書きした `totals` の 2 つの語は、相手のコアからいつもそろって見えます。
- V3F は割り込みで受けた数を `loop1()` に渡し、時間のかかる計算は割り込みの外で行います。
- `Serial1` は `setup1()`（V3F）で始めるので、その割り込みも V3F で受けます。

## 改造するとき

1 つのペリフェラルは 1 つのコアだけが使います（`Serial` は V5F、`Serial1` は V3F）。V3F ではヒープ（`String`・`new`）を使いません。割り込みの中では `ipcSend()` の待ち時間を 0 にし、`hsemTake()` は使わず `hsemTryTake()` にします。CPU Core = V3F でビルドすると V5F が眠ったままなので、`ipcSend()` は false になり、答えは来ません。約束の全体は [CH32RV の README](../../README.ja.md) の「H41x の 2 つのコア」にあります。

## 関連文書

- [スケッチ](DualCoreMessages.ino)
- [基本サンプルの使い方と一覧](../README.ja.md)
- [DualCoreV3F](../DualCoreV3F/README.ja.md)
- [CH32H417 のコアの組み方](../../../../docs/h417-cores-design.ja.md)
