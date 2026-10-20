# EXP-06 X0のタッチ機能除去と専用対照

2026-10-20。入力作成時のHEAD `bca4709`。検査済みhost試作は `efb39e4`。
[旧X0比較](x0-result.ja.md)の実行時flagは残し、別の実験headerでcompile時の有無を比較した。
製品コア・USB実装、凍結した旧X0のソース・測定値は変更していない。

## 五つの条件

| 条件 | タッチと保持 |
|---|---|
| runtime0 | 旧X0を実行時無効で開始。タッチの状態・buffer・bus内の3番目の枠も残る |
| runtime1 | 同じ旧X0を実行時有効で開始 |
| off | 新しい固定feature型でタッチなし。専用状態・buffer・3番目のbus枠を持たない |
| on | 同じ固定feature型でタッチあり。50回／秒、8 byteを追加 |
| manual | タッチ処理とfeature storage／hookを除いた専用対照。offの余分な費用を調べる基準 |

R/A/B/C-static-batchそれぞれをこの五条件へ接続する。
入力容量4、commandとsession各4 byte、1回の入力snapshot上限4、表示32 byte、計測8 byteと回収契約は維持した。
固定型は稼働中に有無を切り替える能力を持たない。runtime0/1の切替可能性まで同等とはしない。
このfrontendでは全五条件とも初期の有無を保ち、途中のenable／disableは入力に含めない。

固定型は有効時だけ周期・count・missed・pendingと8 byte bufferを持つ。
無効時は空のbaseとcompile時分岐を使い、専用対照はそのbaseとhook自体を持たない。
busは有効時3者、無効時2者として、検査用のfail flagと開始countも2者に縮めた。
無効時に3番目の枠へアクセスせず、範囲外のowner要求は受理しない。

busは旧p2::SharedBusと同じsticky終端・借用・busy・hash検査を持つ別の有界型。
新busにはowner範囲検査も追加したため、旧runtimeとのFlash差の全てをflag除去だけの効果とはしない。
固定on−offは機能追加の費用、off−manualは無効featureの仕組みの費用として分ける。
意味の正しさは同じ独立oracleへ照合し、専用対照だけを正解にしていない。

## Host検査

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_x0_features.py tests/unit/test_workspace.py \
  -o junit_family=xunit1
```

ASan／UBSan付き新規88件、配置規則3件の計91件が成功。
既存plugin由来のexperimental warning91件。tests以下へconftest.pyを追加していない。

- 五条件×R/A/B/Cの20構成で1000 tickの用途を照合。PPS確定5020、計測10件、表示2 frame。
  タッチありだけ50件を回収し、最後の表示revisionも一致する。
- off／on／manual×各群で、表示・計測の失敗と回復、入力満杯、複数Applyの集約、
  切断・再接続、古いPD終端、通常処理停止、計測源変更を照合。有効時はタッチの失敗も含む。
- off／on／manual×各群×system／未参加user／参加userで、明示・loop外・delay相当のM/S/U機会、禁止文脈を照合。
- 6 seedの各1000生成入力と後続drainを、全20構成へ同じまま適用。
  同じ有無のoracle結果を再利用しても、各構成の出力は全入力後に29項目を個別照合する。
- 各固定条件のbusyと借用buffer書換えの検出が実際に動くこと、無効時のowner3要求が進行中I/Oを変えないことを検査。
- 無効型にtouch buffer memberがなく、有効型にはあること、無効型と専用対照のsizeofが同じことをcompileで確認。
  無効型を実行時入力で有効にしようとするfrontendは拒否する。

## ABIと未使用除去

```sh
python3 tests/platform/core_redesign/measure_data_parts.py --matrix x0-features \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --out work/exp06-x0-features-link
```

29ケース×2 ABI×LTO有無の116構成がlink成功。
未使用off／onの8条件×4構成、計32行は追加Flash／静的RAM／init_arrayが0 byteで、baseline binaryと一致、関連symbolも残らなかった。
全116行でRAM sectionの和と実配置の占有spanも一致した。
全16対でoffとmanualのFlash・静的RAM差は0 byte、binaryも一致した。
全行は [x0-features-size-matrix.csv](x0-features-size-matrix.csv)、tool・入力・比較対は [x0-features-measurements.json](x0-features-measurements.json)。
RV32E・LTOなしの参考値。各セルはFlash image／静的RAM、単位byte。

| 群 | runtime0 | 固定off | 固定on |
|---|---:|---:|---:|
| R | 3294／372 | 3106／348 | 3310／376 |
| A | 3294／372 | 3106／348 | 3310／376 |
| B | 3778／400 | 3562／376 | 3774／404 |
| C-static-batch | 3906／384 | 3702／360 | 3918／388 |

今回の全16対で、offはruntime0より静的RAMが24 byte少なく、onはoffより28 byte多かった。
固定onは旧runtime1より4 byte多い。状態の配置も異なり、固定化すれば常に小さくなるとはしない。
この結果だけから、他の機能・compiler・ABIでも同じ差になるとはしない。


この容量比較は明示Mの入口に固定した。S/Uはhostで許可と意味を照合し、固定型のS/Uのtarget費用はこの表には含めない。
同じ入力分岐と診断観測を持ち、検査counter・hash・fake I/O・metadataも含む。
旧表とは観測入口も異なるため、旧数値を直接引いてfeature単体の費用としない。
startup、実controller、protocol、IRQ保護と物理clockは含まない。
関数の `.su` はframeの記録で、実stack peakや実時間の保証ではない。

## 再現と後続

host `work/exp06-x0-features-host-20261020/`、link `work/exp06-x0-features-link-20261020/`、RAM配置監査 `work/exp06-x0-features-audit-20261020/` に入力・生記録を保存。
コマンドと終了値、stdout／stderr・JUnit、ELF・map・symbol・section・逆アセンブル・.su、hashを照合した。
移送archive `work/exp06-x0-features-20261020-evidence.tar.gz`、4,945,533 byte、
SHA-256 `901c66711e43ad42c6f02e936c3cf41c526aa4f48807d5bf2f5eb600ee5f5175`。再実行は未作成の出力先を使う。
HEADだけでなく、当時の試作を含むhash付き入力を再現の根拠とする。

これは実験用の型選択と有界busの比較であり、公開APIや製品の機能選択方式の採用ではない。
他の機能でも無効時0 costになることや、実I/O backendの未使用除去まで証明したことにはしない。
Cは引き続き固定1宛先・有限batchで、動的登録を持つ汎用Cとは能力が違う。
時刻は非周回の単調入力。結合X0のwrap、sleep、実PD／I2C、取消・停止は別条件として残る。
次は表示交換・独立library等の変更課題、実I/O接続と完了枠／arbiter、保存・系列・休止を進める。
教材として人が予測・変更できるかの評価と、方式の採用は未実施。
