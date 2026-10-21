# X0 device入力の一次資料照合と計算模型

2026-10-21。実装commit `8520a3b`、通信timeoutの追記 `f6c3be2`。
[固定入力・資料hash](../../x0-device-inputs.json)、[照合と模型の全結果](x0-device-measurements.json)、
[wire計数](x0-device-wire-matrix.csv)、[合成負荷](x0-device-stress-matrix.csv)を保存した。
[上流Uの呼出し観測](x0-upstream-result.ja.md)から、X0へ接続する前に必要なcontroller・計測条件を調べた。
製品コア、既存renderer、P1の用途契約は変更していない。driverや公開方針を採用したものでもない。

## 固定した資料

| 発行元 | 使用した資料 | 取得元と照合箇所 |
|---|---|---|
| Solomon Systech | SSD1306 Rev 1.1、2008-04、59 page | [Newhaven配布のメーカーPDF](https://newhavendisplay.com/content/app_notes/SSD1306.pdf)。p19〜20のI2C、p34〜36のaddressing |
| Texas Instruments | INA219 SBOS448G、2015-12、38 page | [TI公式PDF](https://www.ti.com/lit/ds/symlink/ina219.pdf)。p5・13のtimeout、p9〜10の変換、p12・14〜15の計測／取引、p19〜20・23のregister |

PDF／抽出text／使用pageの画像を保存し、表と図も確認した。pageは1始まりで本文の表示に対応する。
Solomon資料は発行元と配布mirrorを区別する。検索cacheにあったRev 1.2のURLは直接取得が404だったため、固定根拠へ含めない。
この2版を検証入力として固定したのであり、最新資料や実moduleのIC revisionを保証しない。

## 確認したdevice境界

SSD1306ではcontrolのCoとD/C#を区別する。候補で扱うcommand連続列の先頭は0x00、data列は0x40。
7 bit addressはSA0により0x3c／0x3d。水平と垂直でRAM addressの進む順序が異なり、column／page windowは0x21／0x22で設定する。
上流のvertical指定へ、既存page順のpayloadをそのまま流すことを正解にしない。これらは上記SSD1306 PDFのp19〜20・34〜36に対応する。

INA219はpointer指定後に16 bit wordをMSBから読む。電流scaleにはcalibrationとshuntの対応が必要で、通信成功だけでは値の有効性や新しい変換完了を表さない。
CNVR・OVFと、SCLまたはSDAが28 msを超えてLowを継続した場合のinterface reset条件も確認した。
CNVRのclearについてp10の一般説明とp23のregister詳細で挙げる操作が一致しないため、ここではread-clear動作を確定しない。
いずれも上記TI PDFの固定版に対する照合で、実deviceの動作確認ではない。

## Address順の模型

1,024個の位置labelをRAMへ配置する模型では、page順の列をvertical順と誤解すると1,022位置でlabelが一致しなかった。
これはsymbolic labelの対応で、画面上の誤pixel数ではない。
既存のgolden4 frameについては次を確認した。

- page順の列をhorizontalとして配置すると、全RAM hashがgoldenに一致する。
- 同じ列を並べ替えずverticalとして配置すると一致しない。
- 同じrenderer出力をcolumn優先へ並べ替えてvertical配置すると、RAM hashは一致する。wire列のhashとは区別する。
- dataごとにcontrolを付け、payload1／16／31／32 Bに分割しても、模型のRAMは同じになる。
- chunkごとに正しいcolumn／page windowへ設定し直す模型でも、全RAMが一致する。

模型はwindowとpointer継続を仮定する。STOPを跨ぐ継続、reset・他writer・途中失敗がある場合の再同期は実機で確認する。
電源・charge pump・COM／SEG・orientation・panel初期化を含まず、RAM整合を実表示の成立と扱わない。

## Headerを加えたwire計数

128×64のpayload1,024 Bを32 Bずつ送る、二つの未採用条件を数えた。
frame-windowはframe冒頭にmode／windowを1回設定、chunk-windowは各chunkの実位置へ設定し直す。
候補commandはcontrol込み9 B、各data取引はcontrol1＋payload32＝33 B。これはstorageの必要byte数で、classのsizeofではない。

| 条件 | 取引数 | address込みbyte数 | SCL clock数 | 100 kHzのclock部分 | 400 kHzのclock部分 |
|---|---:|---:|---:|---:|---:|
| frame-window | 33 | 1,098 | 9,882 | 98.820 ms | 24.705 ms |
| chunk-window | 64 | 1,408 | 12,672 | 126.720 ms | 31.680 ms |

各byteのACKを含め9 clockとした計算で、START／STOP・setup／hold・stretch・CPU・競合・retry・初期化を除く。
実転送時間の測定値、実効SCL周波数、完了期限ではない。

追加の合成負荷として、毎秒10 full frameと計測pair10要求を置いた。
計測pairはpointer1 B＋word2 Bの2取引、addressも数えると90 clock。
frame-windowではclock計数だけで100 kHzの99.72%、400 kHzの24.93%となる。
chunk-windowでは127.62%／31.905%で、前者はこの需要をclock計数だけでも収容できない。
これはcap下のstress需要で、既存の通常入力traceが常に10 frameを出すという主張ではない。
未確定の物理touch取引を、論理payload8 Bだけから推測して足していない。

## 変換・読出し・用途更新を分ける

上流の固定0x27ffをdecodeするとbus／shunt ADCの設定はともに15だった。
固定資料の表の68.10 msを各量へ適用して合算した**計算模型**は136.20 ms、約7.34 complete cycle／秒となる。
これは実測更新周期や最大時間の保証ではない。基板で選ぶaveragingをここで変更していない。

この周期で完了するcycle tagを定常状態に置き、100 msごとに1秒間読む整数模型を実行した。
1 µs刻みの136,200位相で、新しいcomplete-cycle tagを拾う回数は7または8、同じtagの再読出しは3または2だった。
実registerの更新順、同時sample、CNVR latchを再現した模型ではなく、read回数とfreshな取得数が別になる反例である。

上流が宣言する10 mΩとcalibration4096からのscale計算は1 mA／LSB。
実shunt・誤差・calibrationの保持状態は未確認で、実電流の正確さを証明しない。
全65,536 raw wordについて上流の電圧式と別表現の式を照合し、signed電流decodeも全word域で照合した。
物理的に有効でないraw値も含む代数検査であり、入力範囲や測定精度の試験ではない。

## 通常処理の遅延と外部の期限

28 msのLow継続条件は、全taskがその時間以内に実行されるべきという一般規則ではない。
実行中取引を止めた間にSCL／SDAがどうなるか、相手がresetしたか、周辺に何が残ったかを観測する条件になる。
現在の明示step型register実験は、この外部timeoutとの結合を検証していない。

実機刺激では、各phaseで通常処理を止め、線の状態・停止時間・ACK／error・受信prefix・回収可能性を対応させる。
IRQ側の必須進行と通常側の通知／回収を分けた場合も、同じ外部刺激で比較する。
この資料照合でIRQ方式やloop／delayの自動進行を採用せず、失敗結果・復旧条件と合わせて評価する。

## 続く通常作業と判断境界

- 同じrenderer・packet列をR/A/B/Cへ接続し、header込みstorageとprotocol状態を全群で数える。
- panelの電源・reset・COM／SEG等を明示したfixtureを固定し、初期化・途中失敗・再frameの実挙動を確認する。
- INAの2 register取引を論理8 B結果へ集約し、部分失敗・旧値保持・stale・OVF・calibration喪失を接続する。
- 通信完了、変換完了、用途への反映を別々に記録する。CNVRのclear解釈は追加一次資料または実deviceで確認する。
- bus帯域・変換設定・実進行機会を揃えて実測する。元の仮想tickをphysical msへ読み替えない。

P1のglyph・表示情報・payload上限・用途意味は維持する。比較契約の変更や公開範囲の採否が必要なら高位判断へ戻す。

## 再現と証拠

```sh
uv run --project tests --locked python tests/platform/core_redesign/audit_x0_devices.py \
  --fetch --out work/x0-device-audit-new
```

ローカルcopyの場合は `--pdf-root` と `--upstream-root` を指定する。
2 PDF・上流4 fileの再取得でも、照合・数値模型は同じ結果になった。workspace規則3件も通過。
これはnative firmware試験や実機性能試験ではなく、独立した算術・配置の照合である。

証拠は `work/exp06-x0-device-20261021-evidence.tar.gz`、4,217,530 B、
SHA-256 `43aebbe608d8cce42cd6d4a203068b29c295280fbf0ed33623f88dbe46d3be8a`。
固定資料・page画像・上流sourceとLICENSE・golden・照合tool／出力・JUnit・再集計scriptを含む。
