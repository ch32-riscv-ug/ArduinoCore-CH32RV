# Servo

任意のpinで最大12個のRCサーボを扱えます。1本のtimerが順番に駆動する方式で、
「あるサーボのpinを上げる → そのパルスが終わったら割込みで下げる → 次へ」
を繰り返し、最後まで行ったら20msフレームの残りを待ちます。
AVR版と同じ仕組みで、**pinがtimerの出力に縛られない**理由でもあります。

```cpp
#include <Servo.h>

Servo servo;

void setup() {
  servo.attach(PA1);
  servo.write(90);            // 角度
}

void loop() {
  servo.writeMicroseconds(1500);   // パルス幅でもよい
}
```

## 知っておくとよいこと

- **サーボの電源は別に取ってください。** ストール電流でboardのレギュレータが
  落ちます。症状は「動作中にboardがリセットする」です。
  GNDは共通にし、3V3は共有しないこと。
- **どのtimerを使うか、その代償。** variantが選び、`CH32_SERVO_TIMER`として
  書き出しています。**`tone()`と同じtimerには絶対になりません**
  (ブザーを鳴らしながらサーボを動かすのは普通の要求なので)。
  小さい部品では空きが無く`analogWrite()`とtimerを共有します。
  その場合に影響するpadはvariantヘッダに列挙してあります。
- **`attach()`は失敗します。** pinが無効、12スロットが埋まっている、
  そのseriesに空きtimerが無い、のいずれかで`INVALID_SERVO`を返します。
  戻り値を見ないsketchは黙って何もしません。
- **`write()`は544未満なら角度、以上ならパルス幅**として扱われます。
  この曖昧さはAVR版由来で、sketchの挙動を合わせるために踏襲しています。
- 既定の範囲は0〜180度が544〜2400us。`attach(pin, min, max)`でサーボごとに
  変えられます(多くのサーボはフルレンジより狭い方が安全です)。
- `detach()`でスロットを解放しpinをLowにします。最後の1個が外れるとtimerも止まります。

## examples

- **Sweep** — 端から端まで往復する定番。
