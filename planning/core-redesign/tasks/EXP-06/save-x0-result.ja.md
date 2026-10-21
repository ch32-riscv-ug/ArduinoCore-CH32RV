# N05 X035用途と同期保存・停止時間の共存

2026-10-22。実装commit `9b654b0`。
[入力](../../save-x0-inputs.json)に、X035 C8 route0・direct回収のX0と保存模型の条件を固定した。
[N02の周辺準備・register engine・所有・結果貸出](x0-prepared-wire-result.ja.md)と
[EXP-04の所有付きscanner／writer](../EXP-04/integrated-result.ja.md)を変更せず接続。
製品コア・USBは変更していない。物理Flash driverの実行や保存方式の採用ではない。

## 接続と容量

PPSのdesired値とcaller tagをuint32ずつ、固定blob8 Bとして保存する。
受理時にcopyし、その後アプリがdesiredを変えても保存中の値は変えない。
1消去単位では1 bank、2／4単位では2 bank、header16 B＋CRC32、別footerは使わない比較条件。
すべての形式・APIを選定したものではない。

| 選択 | 予約 | bank当たり | schemaの有効payload | 1更新の媒体操作 |
|---|---:|---:|---:|---|
| 保存なし | 0 B | なし | なし | なし |
| 1単位 | 256 B | 256 B | 8 B | erase1＋program1 |
| 2単位 | 512 B | 256 B | 8 B | erase1＋program1 |
| 4単位 | 1024 B | 512 B | 8 B | erase2＋program1 |

4単位でも履歴やpayloadは増えず、現在のwriterはbank全体を消すので操作が1回増える。
容量を増やせばこの用途で効率が上がる、という設計ではない。menuや最終形式の採用にも使わない。

保存はmount中の領域leaseを保持。readは明示progressの最大32 B、書込みprogressは同期媒体操作を最大1回。
結果は成功／失敗とも保持し、明示takeまで次の保存を受理しない。
take後に再走査を始め、Readyになるまで次の更新を受理しない。アプリ自体の処理は進められる。
失敗後の明示recover、終了、cold-model bootでも所有を守る。
復元したblobは公開するだけで、PPS設定へ自動適用しない。

## 時刻と入力を別にする

同時進行の[raw Flash実測記録](../../../../docs/flash-raw.ja.md)のcommit `bbe0ee7`を参照した。
参照snapshotはSHA-256 `0944cd094f1a1794197070156c02da77c619b03755cfee6d2d87fd043c465903`。
既定入力はerase 2,920 µs、program 1,620 µs、buffer load 68 µs。
実測されたX035 48 MHzの値を感度分析へ入れたもので、今回の用途全体の実測値や上限ではない。

模型は次を分離する。

- 壁時計：同期媒体操作中も進む。仮想PPS完了と仮想touchのdevice時間はこちらを使う。
- アプリが読むms：壁時計換算、または各blackout中のtickを最大1回だけ保持する模型。
  loadの68 µsはblackoutと分ける。実SysTick・millis補正の実装ではない。
- 外部入力：±ボタンを宣言周期で発生させ、容量1／4の仮想latch／FIFOへ保持する。
  deliveryは後の明示通常機会。実GPIOやIRQにこのFIFOがあると仮定しない。

同期操作中はapp opportunity、I2C poll／take、測定完了回収、描画を呼ばない。
callback入口では既存完了数・通常呼出し数・MMIO記録が変わらないことをassertした。
deviceのterminal flagが立つことと、それを通常側で回収することを分ける。
Flashにある実IRQ／USB／PD処理がこの仮想deviceと同じように進むことは証明していない。

`run end quantum`はhost fixtureの明示的な通常機会列。
各回で時刻・入力delivery、I2Cの既存register刺激列、アプリopportunity、保存progressを順に実行する。
I2C刺激は仮想の取引完了で、SCL速度やCPU実行時間は測らない。
同期操作がendを越えた場合も時刻は戻さず、実際の模型時刻を返す。期限保証にはしない。

## Host結果

最終native92件＋workspace3件、計95件成功。既存plugin由来warning190件。
386 trace・60,818コマンドを保存し、全4群の揃う94組で全公開状態が一致した。
R／A／B／C-static-batchの違いだけで停止を回避した結果にはなっていない。

- 保存なしのアプリは既存の独立X0 register oracleへ照合。
- blobの各bankは独立したstruct／CRC decoderで照合。caller値のcapture、未回収、32 B走査、再走査を検査。
- 受信済みI2C結果のloanが保存処理だけでは返らず、後のapp機会で回収されることを検査。
- 1／2／4単位、操作位置ごとの失敗29条件を全4群で検査し、失敗回収→再走査→明示復旧も実施。
- 書込み境界のcold-model bootを12条件で検査。揮発状態・時刻epoch・周辺模型は初期化し、媒体byteを残して再走査。
- seed 0〜9、各100 lifecycleでrun／保存／未回収／再起動／通常処理を組み合わせた。P1の大規模乱数条件全体の充足にはしない。

### 100 msの宣言入力列

1 msに1回の±入力、通常機会quantum 1 ms、50 ms時点で保存を要求した固定例。
値は仮想時刻・仮想入力保持と明示delivery間隔の結果で、実スイッチの取りこぼし実測ではない。

| 保存 | 容量1で失われた入力 | 容量4で失われた入力 | 壁時計型ms | 1 pending tick型ms |
|---|---:|---:|---:|---:|
| なし | 0 | 0 | 100 | 100 |
| 1単位 | 4 | 0 | 100 | 98 |
| 2単位 | 4 | 0 | 100 | 98 |
| 4単位 | 7 | 0 | 100 | 96 |

この列ではI2C回収が全条件71件であっても入力が失われた。
媒体操作後から次の明示deliveryまでのquantumも保持容量に影響する。
「描画やbusが進んだ」だけを用途成立の判定にしない。

別の100 ms×2操作のstress列では、通常完了を回収せずdevice terminalだけが残った。
壁時計型では測定／touchの未処理周期を記録できても、遅れたms模型ではまだ期限に到達していないと見える。
PPSの仮想完了5 ms等を実USB PDの必須期限へ読み替えない。

## 故障模型の限界

媒体はerase／programから戻ると失敗時もquiescentで、borrowed scratchを保持しない。
失敗maskは変更なし／全変更／前半変更。eraseには全0、全0＋先頭byteの不安定readを追加した。
失敗でも新しいblobがCRC-validになる場合があり、false returnだけで「保存されなかった」と推測しない。
1 bankでは旧版を失いEmpty／Corruptとなり得る。2 bankの旧版保持はこの限定媒体模型内の結果。
不安定readは決定的な反例で、CRCによる実電断保護や、未知の揮発的read全体への保証ではない。
operation内の任意時刻で実電源を切った検査ではない。

初回は81件成功・4件失敗。複数progressを含む`run end quantum`を検査側が単発commandと誤認し、
累積readを0と要求したため修正した。lifecycle10条件を追加し、全95件を再実行。
予備probeのAPI名／cold-precondition修正、初回失敗も保存し、成功件数へ加算しない。

## 配置と費用

[42構成](save-x0-size-matrix.csv)はX035 C8 route0、LTO有無、4群×保存0／1／2／4単位と、未使用・empty。
媒体は旧EXP-04と同じopaque hookをLTOなしで使う。物理Flashへアクセスしない。
linkerは末尾予約をNOLOADで確保し、入力dataの混入とappの重なりを拒否する。
その2種類の合成link失敗も検査した。

R、LTOあり、単位B。

| 保存単位 | Flash image | 保存予約 | 静的RAM | うち保存object | うちopaque backend |
|---|---:|---:|---:|---:|---:|
| 0 | 6648 | 0 | 748 | 0 | 0 |
| 1 | 8832 | 256 | 1160 | 400 | 12 |
| 2 | 8856 | 512 | 1160 | 400 | 12 |
| 4 | 8856 | 1024 | 1160 | 400 | 12 |

用途object664 B、共通harness84 Bを含む。保存objectはpage scratch256 B、scanner、writer、台帳、結果状態を含み、
作業bufferを共用した下限値ではない。hostのclock／入力FIFO模型はtargetの機能やRAMとして載せていない。
未使用8行は追加image／RAM／objectが0 Bでemptyと一致。その未使用対照の予約は0単位であり、
利用者が別途明示予約した領域まで返す証拠ではない。
最大単一frameはstack peakではない。startup／実IRQ／物理Flash driver／stack予算込みのB1 fitではない。

最初のABI runは旧matrixの残存変数で0行、2回目は42行成功。
予約のNOBITS／位置・入力data拒否を明示検査する3回目を最終とし、全42 imageは2回目と一致した。
allocator・constructor arrayは検出されなかった。

## 再現と残り

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_save_x0.py tests/unit/test_workspace.py -q
python3 tests/platform/core_redesign/measure_save_x0.py \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --out work/<new-save-x0-abi>
```

`EXP06_SAVE_X0_EVIDENCE`に未作成directoryを指定するとsource・実行物・traceを保存する。
[測定記録](save-x0-measurements.json)にhash・JUnit・trace・ELF・予備run・archiveを残した。
[教材案](save-x0-exercise.ja.md)は人による評価前。

並行作業commit `e6d507f`の[永続化仕様下書き](../../../../docs/persistence-spec.ja.md)は現行core向けの別計画・採用未定と明記されている。
本記録はそれを変更せず、blob8／1 bankを含む既存比較条件を維持する。
N05のX035 host／配置部分を追加した。次はV003 K0の1消去単位を含む共存。
物理backend・全runtime配置・upload保持・IRQ／USB／PD・実電断はN08／N09、形式と公開範囲の採用はP5に残す。
