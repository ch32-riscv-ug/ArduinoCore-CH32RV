# CH32RVGpha

[English](README.md)

CH32H415 / H416 / H417 の画像用の DMA（GPHA、CH32H417RM の 44 章）を使います。画素の四角い範囲を、ハードが塗りつぶし、
画素の形式を変えながら写し、2 枚を合成します。

```cpp
#include <CH32RVGpha.h>
CH32RVGpha gpha;
CH32RVGpha::Surface screen{frame, CH32RVGpha::RGB565, 320};   // 画素、形式、1 行の画素数
gpha.fill(screen, 10, 20, 100, 50, 0xFF2040C0);               // x, y, 幅, 高さ, ARGB
gpha.copy(icon, 0, 0, screen, 40, 40, 32, 32);                // icon の (0, 0) から screen の (40, 40) へ 32 x 32
gpha.blend(sprite, 0, 0, screen, 40, 40, screen, 40, 40, 32, 32, 200);   // sprite を screen に重ねる（全体の α 200/255）
```

## API

| 関数 | 内容 |
|---|---|
| `fill(to, x, y, w, h, argb)` | `to` の四角を色 `0xAARRGGBB` で塗る |
| `copy(from, fx, fy, to, tx, ty, w, h)` | `from` の四角を `to` に写す。画素は `to` の形式に変換する |
| `blend(fg, fx, fy, bg, bx, by, to, tx, ty, w, h, alpha)` | `fg` を `bg` に重ねて `to` に書く。`fg` の α に `alpha / 255` を掛けた重み |
| `setPalette(argb, count, background)` | 索引の形式（L8 など）が引く色の表。`count` 色（1〜256）の `0xAARRGGBB`。前景（`copy()` の元、`blend()` の fg）か、`background` なら `blend()` の bg |
| `setLayerColor(rgb, background)` | A8・A4 の元が使う色（`0xRRGGBB`）。前景か、`background` なら `blend()` の bg |
| `bitsOf(format)` | 1 画素のビット数 |

形式は ARGB8888・RGB888・RGB565・ARGB1555・ARGB4444（入出力）と、L8・AL44・AL88・L4・A8・A4（入力だけ）。L8・AL44・AL88・L4 の
索引の形式は、`setPalette()` で入れた色の表（CLUT）を引きます。A8・A4 は `setLayerColor()` の色に、画素の値を α として付けます。どの呼び出しも
転送の終わりまで待ちます。設定が正しくないとハードが断り（CEIF）、false を返します。GPHA の無い系列では、どれも false です。

## 実機で分かったこと（CH32H417、2026-11-19）

- 塗りつぶしの色のレジスタ（OCOLR）は、出力の形式に関係なく ARGB8888 で書きます（STM32 の DMA2D は出力の形式）。
- RGB565 への変換は上位のビットを残し、RGB565 からの展開は上位のビットを下に写します（RM の表のとおり）。
- 色の表は、GPHA 自身の読み込み（CMAR・CS・START）では入らず、CPU が 1 色ずつ書くと入りました（`setPalette()` はそうしています）。
- 合成は、正確な `(a × f + (255 − a) × b) / 255` より、チャネルごとに最大 3 小さくなります（不透明の白を黒に重ねて 0xFD）。

## 試験

`selfapi::test_gpha`（tests/instrumented/selfapi）が、塗りつぶし・変換つきのコピー（両向き）を CPU の計算と完全に、合成を
−3〜+1 の幅で比べ、四角の外を壊していないことも確かめます。L8 から色の表を引く変換と、A8 に層の色を付ける変換も含めて、H417 で 8 件すべて通っています（2026-11-19）。
