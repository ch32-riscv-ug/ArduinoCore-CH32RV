# oep_reset_trace — reset → setup() の時間と reset 源（worklist P3 行 9）

`reset_probe` sketch は setup() の先頭で PA1 を HIGH にし、`REBOOT` で「rebooting」を出してから PA1 を LOW にして `CH32.restart()` する。
P4 側は GPIO47（← PA1）を `fixture.gpio` で INPUT_PULL_DOWN にし、`fixture.capture`（observer、1 MHz）で PA1 と console TX を見る。
software reset は PA1 の LOW 幅 = restart 命令 → setup() 到達、debug reset は ndmreset で PA1 が floating（pull-down で LOW）→ setup() で HIGH。

```
uv run tests/manual/oep_reset_trace/oep_reset_trace.py [--repeat 5]
```

## 2026-09-22 の結果（CH32X035F8U6、48 MHz）

| reset | → setup() | reset_reason |
|---|---:|---|
| `CH32.restart()`（PFIC SYSRST） | **0.184 ms**（min 0.183 / max 0.185、n=5） | software |
| probe の debug reset（ndmreset 列、確認無し） | 6.8〜138.5 ms、median 16.7 ms | **software**（ndmreset も RCC_RSTSCKR の SFTRST を立てる。`CH32.resetReason()` では区別できない） |

debug reset の幅は probe 側の列（1 ms 待ち × 2、解除 read-back、dmactive 再投入、running 待ち、線解放 → 再 attach）が支配し、
139 ms の外れ値は E158 の「駐留」を再 attach が救った回と考える。DUT の startup そのものは 0.18 ms。

注意: `testcmd.h` の READY は boot から 500 ms 周期で出るので、**READY の到着は boot の目印にならない**（最初この誤りで 498 ms と読んだ）。
E158 の「banner 1.6 ms」も同じ周期の READY を見ていた。boot 時間は GPIO marker で測る。

## 2026-09-22: CH32V003（`--target v003`）

`CH32.restart()` → setup(): **0.342〜0.345 ms**（n=5、X035 は 0.184 ms）、reason=software。debug reset → setup() は未計測: classic ESP32
probe の capture 窓は 400 kHz で 163 ms しか無く、UART 経路の reset 要求と probe の reset 列（ndmreset → 解放 → 再 attach）がその外に出る。
値は probe 側の列で決まる量なので core の評価には要らない。
