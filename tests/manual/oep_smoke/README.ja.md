# oep_smoke — OEP 開発用 probe で sketch を一巡する

`smoke.py` と同じ判定（`<name> READY` → `PING` → `test_<name>.py` の expectations 再生 → `FAIL` 無し・`failures=0`）を、
WCH-LinkE / probe-rs ではなく **OEP v0 probe** で行う。治具は `targets.py` の profile で選ぶ。

```sh
uv run tests/manual/oep_smoke/oep_smoke.py --target x035 --sketch core_api
uv run tests/manual/oep_smoke/oep_smoke.py --target l103 --sketch all --result-json /tmp/l103.json
```

- 書込み: `oep_client.v0.flash_image.program_image`（ESIG preflight、physical page 差分、probe 内 CRC32 verify、reset）
- **console: probe の `target.console`**。DUT の debug module のデータレジスタ（`SerialDMDATA`）で、ピンを使わず、
  どの UART よりも先に使える。sketch 側は `testcmd.h` の `Console`（[TEST_PLAN](../../TEST_PLAN.ja.md) の規約を参照）
- **UART は試験対象**。`test_<name>.py` が `uart` を使う sketch だけ、profile の `uart`（USART 番号, route）を
  `UART <n> <route> <baud>` で DUT に指名し、probe の `fixture.uart` をその線が落ちるピン（`uart_rx` / `uart_tx`）で借りる
- 判定: `smoke.py` の `expectations()` を流用。`dut` はコンソール、`uart` は指名した UART の線

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

- 2026-09-23 x035: basic 14/14 PASS（console は `target.console`、UART sketch は USART4 を指名）
- 2026-09-23 l103: 5/14 PASS。harness は成立（`serial_echo` は指名した USART1 route 1 で、`system_selftest` は
  リセットを跨いで通過）。残りは L103 側の問題で、Pico probe 経由の書き込み verify が不安定なものと、
  `core_api`（`analogWrite` 以降）・`servo_selftest`・`tone_selftest` が RUN の途中から応答しなくなるもの
- 2026-09-22 x035（旧構成、USART4 console）: 14/14 PASS
