# システム系API: ESP32の流儀に寄せる設計調査

日付: 2026-08-25
状態: **調査と提案。実装はまだ何もしていない。**
方針(maintainer、2026-08-25): Arduino標準が無いAPIは**なるべくESP32(arduino-esp32)に寄せる**。
EEPROMは作らない——作るとしても**低レベルなフラッシュ直接アクセスまで**。
コンパレーターは**クラス化**したい。

## 対象と、ESP32側に前例があるか

| 機能 | arduino-esp32の姿 | こちらへの写像(提案) |
|---|---|---|
| 再起動 | `ESP.restart()` | `CH32.restart()` (PFIC SYSRST) |
| リセット理由 | `esp_reset_reason()` (IDF関数を直接使う文化) | `CH32.resetReason()` + `CH32.resetReasonName()` (RCC RSTSCKRの残骸から) |
| watchdog | ESP32は`esp_task_wdt_*`(IDF)。**ESP8266の`ESP.wdtEnable/wdtFeed`が「ESPオブジェクト」流儀の前例** | `CH32.wdtEnable(ms)` / `CH32.wdtFeed()`。IWDGは**一度動くと止められない**石なのでwdtDisable()は出さない(正直に) |
| heap残量 | `ESP.getFreeHeap()` | `CH32.getFreeHeap()` (`_sbrk`とheap末尾から) |
| チップ識別 | `ESP.getEfuseMac()` | `CH32.chipId()` (ESIG UID 96bit) |
| flash直接アクセス | **ESP8266の`ESP.flashRead/flashWrite/flashEraseSector`**が前例(ESP32はPreferences/IDF) | `CH32.flashEraseSector(n)` / `flashWrite(off, buf, len)` / `flashRead(off, buf, len)` |
| sleep | `esp_sleep_enable_timer_wakeup()` + `esp_deep_sleep_start()` | 後述。急がない |
| SPI slave | **coreに無い**。事実上の標準はhideakitaiのESP32SPISlave(キュー式transfer) | 前例が「core外」なので急がず、やるならその形に寄せる |
| コンパレーター | **ESP32に該当ペリフェラル無し**(前例なし) | 新規クラス設計(下記) |

**置き場所**: `ESP`オブジェクトの写像として、既存の`libraries/CH32`(逃げ道 + examples)に
**`CH32`シングルトン**を足すのが素直。ADR-0013の基準2「coreの機能を出すのに必要」で通る。
名前衝突に注意: いま`CH32.h`はincludeするだけのヘッダで、`CH32`という**オブジェクトは未定義**なので空いている。

## 各論

### 1. `CH32.restart()` — 実装可能・検証可能(今すぐ)

PFICのシステムリセット(core_riscv.hの`NVIC_SystemReset`相当、KEY付きCFGR書き込み)。
family差なし(全EVTで同一)。**コマンド規約のsketchで検証できる**:
`REBOOT`コマンド→再起動→bannerが再来→`resetReason()==software`をRUNで確認、
という**再起動をまたぐtest**が書ける(crt0_probeと同じ考え方で、配線不要)。

### 2. `CH32.wdtEnable(ms)` / `wdtFeed()` — 実装可能・検証可能(今すぐ)

IWDG(LSI駆動、KEY/PSCR/RLDRの3レジスタ、base 0x40003000は**全family同一**を確認済み)。
- `wdtEnable(ms)`: LSI周波数から分周とreloadを計算。LSI周波数はfamily差あり
  (V003=128k? 他=40k?)——**ここだけデータ確認が要る**(EVTの`LSI_VALUE`)。
- `wdtFeed()`: KEY=0xAAAA。
- **`wdtDisable()`は提供しない**。IWDGはハードウェア仕様として停止不能。
  ESP8266の`wdtDisable()`を真似ると嘘になるので、無いことを文書化する。
- 検証: 上と同じ再起動またぎで`BITE`コマンド→餌やり停止→リセット→`resetReason()==watchdog`。

### 3. `CH32.resetReason()` — 実装可能(今すぐ)。値はESP32の列挙に寄せる

RCC RSTSCKR(CSR)の残骸フラグ。ESP32の`esp_reset_reason_t`に寄せた列挙:
`CH32_RESET_POWERON / EXTERNAL(NRSTピン) / SOFTWARE / WATCHDOG(IWDG) /
WINDOW_WATCHDOG / LOW_POWER / UNKNOWN`。
読み出し後にフラグをクリアするか(次回のために)は**要判断**——EVTはRMVFで消す。
提案: 初回読み出しでlatchしてRMVF、以後はlatch値を返す。

### 4. flash直接アクセス — **データが先**(page/erase粒度が無い)

方針確認済み: EEPROM互換はやらない。低レベルAPI(`ESP.flash*`の写像)だけ。
**塞いでいるのはデータ**: 消去単位(page size)と書き込み粒度がfamilyごとに違い
(fast erase 256B / standard 1K/4K等)、`ch32-device-data`に表が無い。
→ **上流への依頼リスト(3件)に4件目として追加すべき**:
`family, page_bytes, program_unit_bytes, fast_page_bytes, keys` の粒度の表。
表が来るまで実装しない(手書きで始めると32 family分の誤記リスクを抱える)。
もう1つの前提: **flashを消している間は実行を止めるかRAM常駐が要る**。
zero-wait領域の外で実行しつつ消すのが可能か、familyごとの検討が要る。

### 5. コンパレーター — クラス設計案(前例なしの新規)

ESP32に前例が無いので、このcoreの既存流儀(`CH32SerialSDI`等の
「`CH32`接頭辞クラス + 変異体define」)に合わせる:

```cpp
CH32Comparator cmp(1);                    // CMP1
cmp.begin(PA0, CH32_CMP_REF_VREFINT);     // +入力pad、-入力(padか内部基準)
bool level = cmp.read();                  // いまの比較結果
cmp.onChange(callback, RISING);           // 出力エッジで割込み
cmp.end();
```

現実の制約:
- CMPの構成は**familyでかなり違う**(X035: OPA兼用3ch+PGA、M030: CMP1-3、
  V003: OPA1のみ…)。入力padの選択肢・内部基準の有無・出力先(EXTI/TIM BKIN)が別物
- 必要なdefine(`CH32_CMP1_BASE`、入力padの選択表)は**device-dataの
  `pin_roles.csv`(155c398)にCMP行がある**——これも取り込み後に生成
- → **設計はできるが、生成の前提が155c398取り込み**。実装はその後

### 6. sleep — 設計だけ先(急がない)

ESP32流儀は「wakeup源を設定してから入る」2段階:
`CH32.sleepEnableTimer(us)` / `sleepEnablePin(pin, level)` → `CH32.deepSleep()`。
CH32側はSLEEP/STOP/STANDBYの3段 + familyでwakeup源が違う(L103はLPTIM/自動wakeup)。
**検証にはボードの消費電流測定が要る**ので、機材(電流計)が来るまで実装しない。

## 提案する順序

1. **`CH32`シングルトン(restart / resetReason / wdt)** — データ不要・全family同型・
   再起動またぎtestで実機検証可能。すぐやれる
2. **flash低レベル** — 上流のflash幾何の表待ち(依頼リストへ追加)
3. **コンパレータークラス** — 155c398取り込み後(pin_roles.csvのCMP行から生成)
4. **sleep** — 機材(電流測定)が来てから
5. **SPI slave** — ESP32SPISlave形。優先度は上記より下(coreに前例が無い=利用者の期待も弱い)
