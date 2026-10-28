# PERSIST-01 raw の API の案

2026-10-28 の案。**この案（256 B 以下の系列だけ、関数 1 本）は、M19 で置き換えた。** 利用者の方針で、全系列に同じ関数（範囲、1 単位、消して書く）を出し、それぞれの費用を測ってからコアに入れた。利用者向けの文書は [docs/flash-store.ja.md](../../../../docs/flash-store.ja.md)。下は経緯の記録。

位置づけは [plan.ja.md の 0.5](plan.ja.md) のとおり。推奨は外部の SD や I2C の EEPROM で、内蔵のフラッシュに書くのは上級者向けの選択肢。コアには raw だけを入れ、使い方はサンプル 2 つで示す。`EEPROM.h` は出さない。

費用と実機の結果は [measurements.ja.md の M18](measurements.ja.md)。試作は [proto/api/](proto/api/)。

## API

今の `ch32rv_flash.h`（H41x の DFU が使う `ch32rv_flash_erase` / `ch32rv_flash_write`）に足す。

```c
#include <ch32rv_flash.h>

CH32RV_FLASH_PAGE_SIZE                 // 消して書く単位（V003 64、V103・M030 128、ほか 256）
CH32RV_FLASH_STORE(name, pages);       // フラッシュの末尾に pages ページを確保する
bool ch32rv_flash_write_page(const void *page, const uint32_t *data);
```

- `CH32RV_FLASH_STORE(store, 1);` は、`uint8_t store[1 * CH32RV_FLASH_PAGE_SIZE]` を `.ch32rv_store` の区画に置く。
  - リンカーが、区画をフラッシュの末尾に寄せる（M13）。スケッチが使わなければ 0 B。
  - 読むときは、普通の配列として読む。書くときは、`ch32rv_flash_write_page` を使う。
- `ch32rv_flash_write_page(page, data)` は、`page` のページを消し、`data` の 1 ページ分を書く。
  - `data` は `uint32_t` の並び。語の境界にそろっていることを、型で求める。
  - 偽を返すのは、次のとき。偽のとき、ページの中身は決まらない。
    - `page` がストアの外にあるか、ページの境界に合わない。このときは何もしない。
    - フラッシュが書き込み保護のエラーを返した。
    - 待ちが上限を超えた（約 1 秒）。
  - 中で行う安全は、最低限だけ: 始める前に状態を消す、待ちに上限を置く、書き込み保護のエラーを見る、終わりに CTLR と状態を戻して施錠する、範囲を見る。
  - 割り込みは止めない。`millis()` も補正しない。書いたあとの読み直しもしない（M17）。
- 対象は、消去の最小の単位が 256 B 以下の系列: V003、V00x、M030、V103、L103、X035、V205、V20x、V30x。
  - V407、X315、H41x は、消去の最小の単位が 4 KB なので、`CH32RV_FLASH_PAGE_SIZE` を定義しない。ビルドで止まる。

### 系列による書き方の違い（利用者には見えない）

| 書き方 | 系列 | 1 回に読み込む語 |
|---|---|---|
| ページのバッファに読み込んでから書く | V003、V00x、L103、X035、V205 | 1 |
| 同じ | M030 | 2 |
| 同じ | V103 | 4 |
| 語を直接書いて、最後に始める | V20x、V30x | — |

## サンプル

置き場所の案は `libraries/CH32RV/examples/`。どちらも、UART から数を送ると保存し、リセットすると読み戻す。

| サンプル | 形 | 費用（V003 / X035 / V203） |
|---|---|---|
| [FlashSettings](proto/api/FlashSettings/FlashSettings.ino) | 1 ページを上書き。頭に版と大きさ。変わらなければ書かない | +388 / +464 / +420 |
| [FlashSettingsTwoBanks](proto/api/FlashSettingsTwoBanks/FlashSettingsTwoBanks.ino) | 2 ページを交互に書く。CRC-16 と世代。電源断のときも前の値が残る | +616 / +692 / +652 |

どちらも、raw の費用（+280 / +312 / +292）を含む。

- 版の確認は、スケッチの側に置く。`Settings` を変えたら `kVersion` を変える。別のスケッチが書いたページも、「保存なし」として読む。
- 消したあとの値（0xFF か 0xE339）には頼らない。版と大きさ（2 面は CRC も）で見分ける。
- 頭に、制約を短く書く: 寿命、書き込み直しで消えることがある（経路とツールの版による）、電源断（1 ページの形だけ）、CPU が止まる系列。推奨は外部の SD や EEPROM であることも書く。

## 文書

コアの文書に 1 ページ足す（日本語と英語）。

1. 推奨は外部の SD（[docs/filesystem.ja.md](../../../../docs/filesystem.ja.md)）と I2C の EEPROM（TODO の `I2CEeprom`）。
2. 内蔵のフラッシュの制約（plan の 0.5 の表）。
3. API と、系列ごとのページの大きさ、消したあとの値、書く間に CPU が止まるか。
4. 費用の表（M17、M18）。`millis()` の補正や読み直しを足したいときの費用も示す。

## 決めてほしいこと

1. **関数を 1 本（消して書く）にするか。** 消去と書き込みを分けると、解錠が 2 回になり、費用が増える。消さずに書き足す使い方は、0xE339 の系列ではできない（flash-raw）。案は 1 本。
2. **範囲の検査（+24〜32 B）を入れるか。** 番地を間違えたときに、スケッチ自身のコードを消すのを防ぐ。案は入れる。
3. **4 KB 単位の系列（V407、X315、H41x）を、今は対象から外すか。** 作業用の写しが 4 KB になり、スタックに置けない。H41x には DFU 用の関数が別にある。案は外す。
4. **名前と置き場所。** `CH32RV_FLASH_STORE`、`ch32rv_flash_write_page`、サンプルの `FlashSettings` / `FlashSettingsTwoBanks`、`libraries/CH32RV/examples/`。

## コアに入れるときにすること

- `cores/arduino/sections.ld` に `.ch32rv_store` の区画を足す（[proto/sections.ld.diff](proto/sections.ld.diff)）。
- `ch32rv_flash.h` と `ch32rv_flash.c` に、上の API を足す（[proto/api/core/](proto/api/core/)）。
- サンプル 2 つと文書。サンプルは CI のビルドに入れる。
- 実機: M18 の手順を試験にする（UART で保存して、リセットして読み戻す）。
