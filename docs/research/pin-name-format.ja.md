# R-21: route定数の書式候補(比較のみ、未決)

日付: 2026-08-21
状態: **候補を並べただけ。採用は未決**(ユーザ指示「自動生成なのでいろいろ作ってみて採用を決めてもいい」)
関連: [todo](../todo.ja.md)の「route定数を機械生成する」

## 何のための定数か

`Wire.setPins()`や`Serial.setPins()`へ渡せるpadは、**routeごとに決まった組**しかない。
どの組があるかはvariantヘッダのコメントを読まないと分からないのが現状で、
狙いは**エディタの補完で選択肢が見えること**。

題材はCH32X035のI2C1(6 route)。実データは以下。

| route | SCL | SDA |
|---|---|---|
| 0 | PA10 | PA11 |
| 1 | PA13 | PA14 |
| 2 | PC16 | PC17 |
| 3 | PC19 | PC18 |
| 4 | PC17 | PC16 |
| 5 | PC18 | PC19 |

route 2と4、3と5は**同じpadでSCLとSDAが入れ替わっている**。書式はこれを取り違えないものであること。

## 候補A: フラットな`#define`(ユーザ提示の形)

```c
#define CH32X035_I2C1_0_SCL_PA10 PA10
#define CH32X035_I2C1_0_SDA_PA11 PA11
#define CH32X035_I2C1_4_SCL_PC17 PC17
#define CH32X035_I2C1_4_SDA_PC16 PC16
```
```cpp
Wire.setPins(CH32X035_I2C1_0_SCL_PA10, CH32X035_I2C1_0_SDA_PA11);
```

- ◯ `CH32X035_I2C1_`まで打てば候補が全部出る。`#ifdef`で存在判定できる。Cからも使える
- ✗ 名前が長く、同じ情報(pad名)が2回出る
- ✗ 定数が6 route × 2 role = 12個。SPIやUSARTも足すと1 variantで数百個

## 候補B: namespace + `static inline constexpr`(ESP32系の事例)

```cpp
namespace ch32 { namespace x035 { namespace i2c1 {
  struct Route0 { static inline constexpr uint8_t scl = PA10, sda = PA11; };
  struct Route4 { static inline constexpr uint8_t scl = PC17, sda = PC16; };
}}}
```
```cpp
Wire.setPins(ch32::x035::i2c1::Route0::scl, ch32::x035::i2c1::Route0::sda);
```

- ◯ **階層が見える**。`ch32::x035::`で周辺一覧、`i2c1::`でroute一覧が出る
- ◯ 型が付くので`int`との取り違えを減らせる。名前空間なので衝突しない
- ✗ **`#ifdef`で存在判定できない**。「このseriesにI2C2はあるか」をpreprocessorで書けなくなる
- ✗ **C(`.c`)から使えない**。coreの`wiring_*.c`は今もCで書かれている
- ✗ 呼び出しが長い。`using namespace`で短くはできる

## 候補C: routeを丸ごと1つの定数にする

```cpp
Wire.setRoute(ch32::x035::i2c1::route0);   // pad2つを別々に渡さない
```

- ◯ **SCL/SDAの取り違えが原理的に起きない**(route 2と4の事故が消える)
- ◯ 定数の数がroute数まで減る
- ✗ 「このpadを使いたい」から入る人には遠回り。Arduinoの`setPins(a, b)`慣習からも外れる
- △ 既に`setRoute(番号)`はあるので、**名前付き定数を足すだけ**で実現できる

## 候補D: `enum class`

```cpp
enum class X035_I2C1_Route : uint8_t { PA10_PA11 = 0, PA13_PA14 = 1, PC16_PC17 = 2 };
```

- ◯ 補完が効き、型で守れて、値はroute番号そのもの
- ✗ pad名からの引きが弱い(`PC16`で検索して出るのは2つのenumerator)

## 決めるときに見るべきこと

- **Cから要るか**。coreの実装はCが残っているが、**sketchはC++**なので、
  「sketchから使う定数」に限ればBやDでも困らない
- **`#ifdef`が要るか**。ライブラリが「この板にI2C2があるか」を聞く手段は、
  今のところ`#if defined(CH32_I2C2_SCL)`しかない。BやDを採るならこれは別に残す
- **量**。全周辺 × 全route × 全roleを1 variantに出すと数百行。
  生成は機械なので苦にならないが、**プリプロセッサの負荷とIDEの補完速度**は実測したい
- 併存もできる。「機械生成なので複数の形を同時に出す」のは実際に可能で、
  片方を非推奨にする移行もしやすい
