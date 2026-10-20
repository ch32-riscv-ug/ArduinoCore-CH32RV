# H417共有予約の容量選択と未使用除去

2026-10-21。[固定SDK配置](h417-layout-result.ja.md)を基準に、共有予約0／256 B／1 KiB／32 KiBをlink上で比較した。
実装commitは`e07e30d`。[入力](../../h417-reservation-inputs.json)、[全結果とhash](h417-reservation-measurements.json)、[16 imageの表](h417-reservation-matrix.csv)を保存した。
製品コア・USB・Flash・SDK原本は変更せず、実起動・uploadも行っていない。
公開設定や方式の採用はP5へ渡す。

## 派生scriptで変える範囲

固定manifestに一致するSDK linkerを読み、実験directory内だけに派生scriptを生成する。
SRAM終端`0x20180000`から共有容量を差し引いてoriginを決め、V3Fの通常RAM終端もそこへ移す。
V5Fの通常RAMはDTCMなので容量を変えない。Flash・code用RAM・load用RAM・stack容量は基準と同じ。
16 byte境界を満たさない値、負数、固定SDKの32 KiBを超える値は丸めず拒否する。
これは実験の探索範囲であり、32 KiBがH417の共有memory上限という意味ではない。

容量があるときは、V3F側でcompileした256 Bのopaque共有objectを両imageへ取り込む。
0のときは共有参照をcompile時に除去し、objectをlinkせず、V5F scriptの明示INPUTも除く。
0長の`RAM_SHARED`と容量ASSERTを残し、明示的に共有objectを混入させた場合はlink失敗にする。
未使用objectを黙って通常RAMへ移す処理ではない。

`__experiment_shared_capacity`・`origin`・`version`を両imageのabsolute symbolに記録する。
versionは今回1。これらのsymbol自体はFlash／RAMへstorageを割り当てない。
link後のpair検査で両imageの設定を照合する。runtimeが読んでhandshakeする実装は含まない。

## 予約と通常RAMの結果

GCC 14.3.0、generic `rv32imafc_zicsr/ilp32f`、`-Os`、LTO off／on。
4容量×2 LTOの8 pair、16 ELFが通過し、各容量の配置結果はLTOで同じだった。
入口はlink probeであり、実startup・vector・IRQ・core起動を提供しない。

| 予約 B | shared origin | payload B | V3F通常RAM B | V3FのBSS終端→stack gap B | V5F通常RAM B |
|---:|---|---:|---:|---:|---:|
| 0 | `0x20180000`、長さ0 | 0 | 458,496 | 456,388 | 261,376 |
| 256 | `0x2017FF00` | 256 | 458,240 | 456,132 | 261,376 |
| 1,024 | `0x2017FC00` | 256 | 457,472 | 455,364 | 261,376 |
| 32,768 | `0x20178000` | 256 | 425,728 | 423,620 | 261,376 |

V3Fには予約を減らした分だけ通常RAMを戻せた。未使用では固定予約に対して32 KiB増える。
V5FのDTCMや通常RAMが32 KiB増えるわけではなく、同じ物理領域を両core分として二重に数えない。
V5Fのgapは259,268 Bで不変。各imageの`.data`8 B、BSS52 B、stack予約2,048 Bも不変だった。
gapは小さいprobeの値で、完成用途の空き容量ではない。

未使用条件は、共有symbolなし、共有section使用量0 B、共有予約0 Bを確認した。
ELFには0 Bの`.shared` section名が残ったが、sharedとしてallocatedなmemoryはない。
V3Fのstackは返した通常RAMの末尾へ移るため、旧共有範囲にprivate stackのLOAD segmentが現れる。
それを「共有storageが残った」とは数えず、各segmentの用途と予約範囲で照合する。

Flash向けLOAD segmentのfile byte合計は、有効時112 Bずつ、無効時V3F 96 B・V5F 100 Bだった。
共有参照の除去と入口の定数・address生成を含む値であり、汎用共有機能のcode費用だけを分離したものではない。
独立probeや各imageのstack等まで0 Bになったという結果でもない。

## 拒否と検査

4容量それぞれで、予約より16 B大きいobjectをV3F／V5Fへ別々にlinkし、全8条件を拒否した。
0容量では16 Bの共有objectが混入した場合に相当する。KEEPと容量ASSERTにより、参照がなくても拒否する。

さらに、単独ではlinkが成功したimageを混ぜて次の3 pairを検査し、すべて設定不一致で拒否した。

- V3F 256 B予約＋V5F 1 KiB予約。
- V3F未使用＋V5F 256 B予約。
- V3F 256 B予約＋V5F未使用。

この不一致は**pairのlink後照合**で検出した。別々のimageを単独でlinkするだけでは検出しない。
異なる時点でuploadしたimage、片側だけの再起動、古いruntimeとの混在を安全にするには、起動時の構成確認とhandshakeが別途必要。

board-freeの予約検査53件＋workspace規則3件、計56件も通過した。conftest.pyは追加していない。
境界・不正容量・source形状・設定symbolの欠落／不一致、未使用条件でも残るprivate初期化・stack・load範囲の検査を含む。
有効な予約は従来のELF／map配置検査を再利用する。
無効な予約も、sharedがないことだけで成功にせず、entry・ABI・MEMORY・data／BSS／stack・Flash load・allocated sectionの重複を照合する。

## 次の接続に残す条件

これはlinker上の可用領域を比較した結果。実startupのmemory設定・アクセス許可・cache・DMA・可視性を確認していない。
opaque byte配列にはC++ objectを構築しておらず、gate参照を持つ既存所有部品をそのまま共有した証拠にもしていない。
有効時のNOBITS／LOAD segmentと、loaderがzero化する可能性は[前の配置結果](h417-layout-result.ja.md)から未解決のまま。

共有表現とcore-local view、初期化owner・起動handshake・旧writerの静止、実行器への接続と共有実行器比較が残る。
容量をboard menuにするか、build設定にするか、link自動決定にするかは未決定。
両imageの設定生成と検査を一体で行う必要があるという材料を得た段階である。

## 再現

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_h417_reservation.py tests/unit/test_workspace.py -q

python3 tests/platform/core_redesign/build_h417_reservation.py \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --bundle work/exp05-h417-research-20261021 \
  --out work/h417-reservation-new
```

新しい出力directoryを使う。bundleには固定manifestと一致するSDK linkerのcopyが必要。
保存archiveは`work/exp05-h417-reservation-20261021-evidence.tar.gz`。
原script・派生script・source・compiler／binutils hash・全command・ELF／map／symbol／segment／disasm・拒否診断・JUnit・analysisを含む。
archiveのhash・byte数は測定JSONを参照する。

教材では、256 B payloadでも予約1 KiBと32 KiBで残るRAMが違う理由、未使用でも0長regionとASSERTを残す理由、
単独linkとpair照合と起動handshakeのそれぞれで何を確認できるかを予想する。
人による教材実施・理解度レビューは未確認。
