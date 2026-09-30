# 手動テストと実機の道具

人の操作か臨時の機材が要るもの、そして LinkE のベンチを扱う道具の置き場です。自動でよいものはここにはありません:
実機の自動テストは [`../bench/`](../TEST_PLAN.ja.md)（`pytest bench --profile <board>`）です。

**入口は `pytest` で、ファイルを名指しします。** `test_` 接頭辞は付けず、`manual` は `norecursedirs` にも入っています
（引数なしの `pytest` が実機を焼かないための二重の防護）。`-s` は必須です（オペレータへの指示が端末に出ます）。

```sh
cd tests
uv run --env-file .env pytest manual/<case>/<case>.py -v -s
```

## 中身

| ディレクトリ | 内容 | 経路 |
|---|---|---|
| [`gpio_loopback/`](gpio_loopback/) | ジャンパ 1 本で GPIO: レベル / pull-up / pull-down / 別ポートへの EXTI / PWM duty。pad は `.env`（`CH32_LOOPBACK_OUT` / `_IN`） | LinkE |
| [`i2c_loopback/`](i2c_loopback/) | ジャンパ 2 本 + pull-up で Wire の slave: I2C1（master）↔ I2C2（slave） | LinkE |
| `uiapduino_fixture/` `uiapduino_pin_map/` `uiapduino_timer_fixture/` | UIAPduino（V003）の治具と pin 表 | — |
| [`chip_info/`](chip_info/) | **いま何が繋がっているか**: probe / chip / serial port / FQBN / Serial の pin | LinkE |
| [`uart_scan/`](uart_scan/) | board がどの USART route を配線しているかを特定 | LinkE |
| [`probe_switch/`](probe_switch/) | USB/IP で 1 台を WSL へ渡し、他を外す（WSL のベンチ専用） | LinkE |
| `smoke/smoke.py` | 上の道具が共有するライブラリ（probe の列挙、board の解決、build / upload、chip の再検出）。sketch の再生（`--sketch all`）は bench/ に移って無くなった | LinkE |
| `conftest.py` | LinkE の道具の fixture（`attached` / `bench` / `uart_routes`） | |
| `env_config.py` | `.env` の pad 名（`PA0`）を pin 番号にし、sketch 用の header を書く（ジャンパの試験が使う） | |
| `bench.json` | この作業台の USART route の上書き（今は空） | |

LinkE 経路のものは probe-rs / ch32rv を直接呼ぶ古い形です。正式ベンチ（家系ごとの P4 + 開発基板）が揃ったら、ジャンパの試験は
配線を常設にして bench/ へ、道具は ch32rv の `probe list` と bench file で置き換えるか消します。

以前ここにあった OEP の runner（`oep_smoke`、`oep_*_trace`）、`reg_probe`、`crt0_probe` は bench/ の test になりました。
中身と実測の記録は git の履歴と [docs/development-workflow.ja.md](../../docs/development-workflow.ja.md) にあります。

## chip_info — まず現状を確認する

```sh
cd tests && uv run --env-file .env pytest manual/chip_info/chip_info.py -v -s
uv run tests/manual/chip_info/chip_info.py           # CLI でも（同じ関数）
```

probe が応答し、chip を名乗り、probe-rs がその chip を知っていて、`boards.txt` が引き当て、variant に Serial がある、の 5 点を
1 assertion ずつ確かめます。読み出しだけで、書き込みも reset もしません。

## uart_scan — 配線が不明なとき

```sh
cd tests && uv run --env-file .env pytest manual/uart_scan/uart_scan.py -v -s
```

**どれか 1 つの route が届くこと**（届かなければ配線）と、**variant が選んだ route が届くこと**（届かなければ生成器の選択）の 2 つ。
結果は `bench.json` へ。

## probe_switch — board の切り替え（WSL のベンチのみ）

```sh
uv run --env-file tests/.env tests/manual/probe_switch/probe_switch.py         # 一覧
uv run --env-file tests/.env tests/manual/probe_switch/probe_switch.py V103    # 切り替え（.env の CH32_PROBE_<NAME>）
```

名前は USB serial で持ちます（COM 番号は物理 port に、bus id は挿し直しに付いてくる）。外すのは WCH-Link だけで、
他の device が埋めているときは何も外さずに止まります。

## gpio_loopback / i2c_loopback

```sh
# .env に CH32_LOOPBACK_OUT=PA0 / CH32_LOOPBACK_IN=PB0（別ポートの空き pad。SWD の pad は不可）
cd tests && uv run --env-file .env pytest manual/gpio_loopback/gpio_loopback.py -v -s
```

ジャンパを忘れると `level_through_wire` が落ちます（HIGH と LOW の両方を見るため）。i2c_loopback は 2 本 + pull-up。

## 結線（LinkE のベンチ）

**TX → probe の RX、RX → probe の TX**、GND 共通。`Serial` がどの USART になるかは series 単位で、生成器が reset 既定の route を
選びます（`variants/<board>/pins_arduino.h`）。bench file（`../benches/<name>.toml`）の `[uart]` がベンチごとの実際です。
