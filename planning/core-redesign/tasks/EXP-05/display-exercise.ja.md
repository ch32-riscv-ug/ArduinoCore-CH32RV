# 表示を交換しても資源を追えるか

この課題は[mock表示交換](display-result.ja.md)を使う。実controller向けの公開APIではなく、利用者による評価は未実施。

## 変更する層を分ける

128×64 monoの32 B chunkを、次のいずれかへ交換する。

- 同じpixelを16 B chunkで送る。
- 128×32へ収め、接続・staleの状態表示を横へ移す。
- 96×64のRGB565 mock streamへ展開し、64 B chunkで送る。

PPS入力・計測・タッチの状態機械を変更せずに済む部分と、Panelのbyte形式・配置・転送量の変更を分けて挙げる。
同じbusへつなぐ実controllerなら、初期化・command・座標・byte順・最大transactionの確認も必要になる。
mockで同じ情報を表示できたことだけで、その実通信が成立したとは扱えない。

## frame全体を持たない

12,288 Bのframeを64 B bufferで送るとき、bufferをいつ書き換えてよいか予測する。
Viewの最新値と凍結値を別に持つ理由、借用中prepareを拒否する理由を書く。
途中で目標値・測定値が変わっても、現在のframeへ混ぜず次へ集約する流れを追う。

小さいbufferにする代わりに再計算が増えること、実CPU時間はまだ測っていないこともRAM表と一緒に読む。
幅や色数の大きいframeをそのまま常設storageにしない選択肢があるが、すべての描画や通信へ自動適用できるわけではない。

## 31 byteにしたら32 byteより小さいか

Compact32の最後のchunkは16 Bになる。常に31 B送る実装にすると何が余るか。
payload上限31 Bでも、Screenのsizeofは32 B版と同じ116 Bだった理由をalignmentも含めて説明する。
payload量、chunk buffer、object全体、harness込みのELF RAMを別々に計算する。

## 小分けの費用と反応

同じmono画像でchunkを32→16 Bにすると、2 frameのtransaction数は64→128へ増える。
仮想wireでは2 frameの完了が220→292 tick、タッチ最大遅れが7→6 tickだった。
「細かくすれば常に速い」とせず、占有の長さ・開始の固定費・byte数・進行機会を分ける。
実I2C／SPIならどの値を測り直す必要があるかを列挙する。

## 失敗したframeをどう見るか

途中のchunkで失敗し、その時点までのpixelは実panelへ届いていたかもしれない。
最後の成功frameのhashを持つことが、画面を元へ戻せた意味ではない理由を書く。
用途に必要な部分更新、再送、全描画、表示off等の方針を比較する質問として残す。

## libraryが裏で進まないことを確認する

`display_parts.hpp`はCoreのqueueや実行器をincludeしない。View更新・chunk準備・受理・完了・stepを誰が呼ぶかを線で結ぶ。
独立directoryと`-nostdinc++`のbuildが証明する依存の範囲と、証明しない実wire・時間・IRQ安全性を区別する。
この構成を独立部品として使う場合でも、app側にはbusの所有と明示回収を接続する責任が残る。
