# GCC から clang へ移せるか（調査）

2026-11-11 の調査の記録です。移すかどうかは、まだ決めていません。

## 試したもの

- **clang:** LLVM 23.1.5（2026-11-03、公式の Linux x64 版）。clang、lld、llvm-ar を使いました。
- **比べる相手:** 今のコアの GCC（xpack 15.2.0）。
- **やり方:** GCC の名前を持つラッパーを作り、Arduino のビルドの C と C++ のコンパイルだけを clang に回しました。.S のアセンブリ、newlib nano、libgcc は GCC のものです。LTO のときは、リンクを clang の driver と lld、ar を llvm-ar にしました。ラッパーは作業用のディレクトリにあり、リポジトリには入れていません。
- **大きさの数え方:** size の text（.text と定数）です。lld の版は data の列にスタックの 512 バイトが入るので、data は比べていません（下の「リンカースクリプト」）。

## WCH 向けの拡張

- LLVM には、WCH の QingKe の独自の圧縮命令「XW」が `Xwchc` として入っています（2024 年の PR #97925。WCH は仕様を公開していないため、WCH の道具から解析した符号化を WCH に確かめてもらったもの）。
  - 32 ビット専用で、D 拡張や Zcb とは一緒に使えません。
  - LLVM 23.1.5 では、実験扱いの指定無しで `-march=rv32ec_xwchc` が通りました。`lbu` と `sb` が 2 バイトの命令になります。
- GCC（xpack 14.3 / 15.2）は `xw` も `xwchc` も知りません（docs/h417-cores-design.ja.md）。WCH 版の GCC（MounRiver）だけが XW を持ちます。
- **XW を持つのは一部の QingKe だけです。** 持たないコアで使うと、実行中に不正命令で止まります。どの系列で使えるかは、系列ごとに確かめる必要があります（V003 の QingKe V2A は持つ。ch32fun も V003 で使っている）。

## 大きさ（CH32V003、バイト）

### LTO 無し（Optimize メニュー「Smallest (-Os) without LTO」）

| 例 | GCC -Os | clang -Os | clang -Oz | clang -Os + Xwchc | clang -Oz + Xwchc |
|---|---|---|---|---|---|
| Fade | 4,556 | 4,492 | 4,072 | 4,400 | 3,960 |
| SerialEcho | 4,928 | 4,960 | 4,692 | 4,884 | 4,604 |
| SystemInfo | 4,600 | 4,636 | 4,296 | 4,564 | 4,212 |
| I2CScanner | 6,416 | 6,864 | 6,376 | 6,652 | 6,172 |
| BasicWork | 5,236 | 5,420 | 4,912 | 5,320 | 4,796 |
| ListProfiles | 4,068 | 4,260 | 3,928 | 4,180 | 3,828 |

### LTO 有り（今のコアの既定は GCC + LTO）

| 例 | GCC + LTO（今） | clang + LTO -Os | clang + LTO -Oz | clang + LTO -Oz + Xwchc |
|---|---|---|---|---|
| Fade | 3,416 | 2,920（-15%） | 2,752（-19%） | 2,664（-22%） |
| SerialEcho | 4,348 | 4,492（+3%） | 4,244（-2%） | 4,184（-4%） |
| SystemInfo | 4,008 | 4,548（+13%） | 3,912（-2%） | 3,856（-4%） |
| I2CScanner | 5,708 | 6,248（+9%） | 5,688（0%） | 5,524（-3%） |
| BasicWork | 4,364 | 3,988（-9%） | 3,680（-16%） | 3,620（-17%） |
| ListProfiles | 2,892 | 2,852（-1%） | 2,732（-6%） | 2,680（-7%） |

- clang の -Os は、GCC の -Os より大きくなることが多い（最大 +13%）。比べるなら clang は -Oz です。
- clang -Oz + LTO + Xwchc は、今の GCC + LTO より 3〜22% 小さくなりました（6 つとも小さい）。Xwchc の分は 1〜3% です。
- GCC の -Oz は、RISC-V では -Os と有効になる最適化が同じで、出てくるコードも 1 バイトも違いませんでした。
- 速さは測っていません（-Oz は速さを犠牲にすることがある）。

## 動くか

- clang + LTO + lld で作った SystemInfo（CH32V103）を、ベンチで書き込んで動かしました。millis() は進みました（デバッガで変数を読んだ。この LinkE の先の配線では UART が見えなかった）。V003 の実機は OEP のプローブの後ろにあり、今回は試していません。
- C++（libstdc++ のヘッダーは GCC のもの）、コアの割り込みの関数（`__attribute__((interrupt))`）、newlib の printf の置き換えも、そのまま通りました。

## 移すときに要ること

| 項目 | 内容 | 量 |
|---|---|---|
| ツールの配布 | パッケージの index に clang と lld を入れる（Windows、macOS、Linux）。公式の版は 1.9 GB（展開して必要なものだけで約 670 MB）。必要なものだけを詰め直す仕組みが要る | 大 |
| libc と libgcc | 今回は GCC の newlib nano と libgcc を借りた。clang だけにするなら、picolibc と compiler-rt を全部の multilib（rv32ec、rv32imac、rv32imafc など）で作る | 大（借りるなら小） |
| platform.txt | clang 用のレシピ。GCC だけのフラグを外す（`--specs=nano.specs`、`-fsingle-precision-constant`、`-msave-restore`、`-Wl,--no-warn-rwx-segments`）。.S を clang の組み込みのアセンブラで通るか確かめる（今回は GCC で組んだ） | 中 |
| リンカースクリプト | lld は `. = . + 大きさ` だけの .stack を中身のある区画にした（size で data に数えられ、objcopy の .bin に RAM の番地までの空白が入るおそれ）。`(NOLOAD)` を付ける。ほかは V003、V103、H417 の V5F の形（`--enable-non-contiguous-regions` も lld にある）で通った | 小 |
| Xwchc | 使える系列の表。間違えると実行中に止まるので、系列ごとに実機で確かめる | 中 |
| 浮動小数点のメニュー | `-fsingle-precision-constant` が clang に無い。float の定数の扱いが変わる | 小〜中 |
| 試験 | 全部の系列で、例のビルドと実機の試験を流し直す | 大 |
| 外部のライブラリ | GCC 前提の書き方（`#pragma GCC optimize`、GCC だけの属性や組み込み関数）が通らないものがありうる | 不明 |

## 得失

**得:**
- 小さい系列で 3〜22% 小さくなる（-Oz、LTO、Xwchc）。V003 の 16 KB では効く。
- WCH の XW が使える（GCC では WCH 版の GCC しか無い）。
- LLVM は新しい版が早く出る。lld も、ITCM からあふれさせる仕組みを持つ。

**失:**
- ツールの配布が大きく、複雑になる（今の xpack の GCC は 1 本で libc まで入っている）。
- libc を GCC から借りる形は、2 つのツールチェーンを配ることになる。
- clang の -Os は GCC より大きいので、-Oz にする（速さは未確認）。
- Xwchc は系列を間違えると止まる。
- 全系列の試験をやり直す費用。

## 次に確かめること

1. 速さ（-Oz と GCC の -Os、CRC や memcpy などの測定、H417 と V003）。
2. 大きい系列（X035、V307、H417）での大きさ。
3. .S（crt0 とベクター）を clang の組み込みのアセンブラで通す。
4. picolibc と compiler-rt で GCC を使わずに作れるか。作ったものの大きさ（newlib nano との差）。
5. どの QingKe が XW を持つか（V2A、V3A、V4 など）。
6. 配布の形（必要なものだけの LLVM を xpack と同じように配れるか、既存の配布物があるか）。
