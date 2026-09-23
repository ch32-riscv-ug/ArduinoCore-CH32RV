# oep_smoke — OEP 開発用 probe で sketch を一巡する

`smoke.py` と同じ判定（`<name> READY` → `PING` → `<name>/expect.py` の再生 → `FAIL` 無し・`failures=0`）を、
WCH-LinkE / probe-rs ではなく **OEP v0 probe** で行う。治具は `targets.py` の profile で選ぶ。

```sh
uv run tests/manual/oep_smoke/oep_smoke.py --target x035 --sketch core_api
uv run tests/manual/oep_smoke/oep_smoke.py --target l103 --sketch all --result-json /tmp/l103.json
```

- 書込み: `oep_client.v0.flash_image.program_image`（ESIG preflight、physical page 差分、probe 内 CRC32 verify、reset）
- **console: probe の `target.console`**。DUT の debug module のデータレジスタ（`SerialDMDATA`）で、ピンを使わず、
  どの UART よりも先に使える。sketch 側は `testcmd.h` の `Console`（[TEST_PLAN](../../TEST_PLAN.ja.md) の規約を参照）
- **UART は試験対象**。`expect.py` が `uart` を使う sketch だけ、profile の `uart`（USART 番号, route）を
  `UART <n> <route> <baud>` で DUT に指名し、probe の `fixture.uart` をその線が落ちるピン（`uart_rx` / `uart_tx`）で借りる
- 判定: 各 sketch の `expect.py`（`smoke.py` の `expectations()` で読む）。`console` はコンソール、`uart` は指名した UART の線。どう繋ぐかは経路ごとに違い、runner が持つ

治具ごとの違いは profile に閉じている。コンソールの配線は要らないので、profile が持つのは「試験対象の UART を
どの USART・route で出せば probe が受けられるか」だけ。

| target | probe | DUT | UART under test |
|---|---|---|---|
| `x035` | ESP32-P4（`examples/Esp32P4X035Probe`） | CH32X035F8U6 | USART4 route 0（PB0/PB1 → probe 12/6） |
| `v003` | classic ESP32（`examples/Esp32V003Probe`） | UIAPduino CH32V003 V1.4 | USART1 route 0（PD5/PD6 → probe 22/21） |
| `l103` | RP2350（`examples/Rp2350L103Probe`） | CH32L103C8T6 | USART1 route 1（PB6/PB7 → probe 13/12） |

probe firmware は別途転送しておく（**`target.console` を持つ版が要る**。無ければ runner がそう言って止まる）。
client は隣の checkout `../../dev_oep/oep-client-python/src` を既定で参照する（`--oep-client` で変更）。
port は profile の値を使い、`--port` か `OEP_PROBE_PORT` で上書きできる。

## 書き込み直後の最初の 1 往復

probe が書き込み後やリセット後に行う確認（halt して PC を読む）は abstract command を使い、abstract command は
コンソールと同じ DATA0 を通る。既に走り始めた sketch はそれを host からの入力として読むので、**最初の 1 行はゴミに
なりうる**。`sync()`（改行を送ってからトークン付き PING を最大 3 回）で同期してから本題に入るので、手動ツールも
READY を待つときは `oep_smoke.sync()` を使うこと。

## 実績

- 2026-09-24（SerialDMDATA の待ちを時間基準にした後）: x035 / v003 / l103 すべて 14/14。x035 と v003 は WCH-Link 7 台の
  同時実行と並行で回した。probe 側で「答えの書込みを読み戻して再送」も試したが、v003 で同じ文字が続く箇所
  （`2.00`、`deadbeef`）を 1 文字ずつ落としたので取り消した。フレームに通し番号が無く、target が同じ内容の次の
  フレームを置いたのと書込みが落ちたのとを区別できないため。飛び線での二重受信（下の l103 の 1 件）は残る
- 2026-09-23(ベンチ再起動後、ch32rv 0.9.1 / V103 M モード化の後): x035 14/14、v003 14/14（`Esp32V003Probe` を
  `target.console` 入りに更新してから）、l103 13/14。l103 の 1 件は hooks_selftest で、判定は全部 PASS していたが
  コンソールの 1 フレームが二重に届き（`failures==0`、`READY READY`）完了行が一致しなかった。probe が target の
  フレームに答える DATA0 の書込みを確かめていないため（飛び線で書込みが黙って落ちると同じ word をもう一度読む）
- 2026-09-23 x035 / l103: basic 14/14 PASS（最終形: probe 59c06f1、QingKe V4 の割込みを gintenr で、testcmd.h の clock heal 入り）。
  l103 は probe 側で DM read の実行回数検査と flash の CTLR/ADDR 読み戻しを入れるまで、書き込み verify が黙って壊れていた。
  書き込み直後の attach が失敗したときは NRST を 1 回入れて再試行する
- 2026-09-23 x035: basic 14/14 PASS（console は `target.console`、UART sketch は USART4 を指名）
- 2026-09-23 l103: 5/14 PASS。harness は成立（`serial_echo` は指名した USART1 route 1 で、`system_selftest` は
  リセットを跨いで通過）。残りは L103 側の問題で、Pico probe 経由の書き込み verify が不安定なものと、
  `core_api`（`analogWrite` 以降）・`servo_selftest`・`tone_selftest` が RUN の途中から応答しなくなるもの
- 2026-09-22 x035（旧構成、USART4 console）: 14/14 PASS
