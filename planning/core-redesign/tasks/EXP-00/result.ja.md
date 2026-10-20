# EXP-00 再現と記録の結果

2026-10-20。P2のEXP-00は完了。
固定probeの18構成を2回実行し、36 build結果の数値・symbol・ELF・binary・正規化map・archive抽出が一致した。
過去の `execution-link-cost-20261019-v2` の18行とも一致した。
P2全体は進行中で、用途の対照実装と物理条件の固定が残っている。

## 条件と入力

- 開始時HEAD：`ba9c3622730bd879e51a52e2270377744ae0f707`、着手時の作業ツリーはclean。
- [依頼](brief.ja.md)と比較仕様P1-1に従い、既存probeの入力C++・linker・9ケース・2 ISA/ABIを維持。
- xPack GCC 14.3.0-1、`-Os`、LTOなし、section分離・gc-sections、例外・RTTI等は元のflagsのまま。
- 実際のcore／CRT／Arduino libraryをリンクしない、独立した最小プログラムのcross compile/link。
- 実行担当は通常実験フェーズ。モデルの実ID・設定は未記録。

入力probeのSHA-256は依頼時と同じ
`2971233136e1459e0a15ae65090ed6a5e5118d8854c5a4a2177afb48f200178b`。
追加した記録スクリプトのSHA-256は
`e876db24279210a68d89f9af911f668107da6f45799b0fedcefb1399f0ed0e37`。
記録用の検査ソースは
`7c666131c56d517a42a0ca4c5b505686990881d12f2383330ae7c39bb310b6fd`。

driver、cc1plus、assembler、linker、collect2、ar／gcc-ar、size、objcopy、nm、objdumpとABI別libgccのhashを保存した。
内部helperの単独version取得は行わず、hashで識別する。libgccは本probeではリンクしていない。
配布archiveはcacheに存在し、414,461,685 byte、lockのSHA-256と一致した。
archive hashは `be1768ef22789f4d9c41384e0261996f51724b84c2efa940d975dd7d9938c726`。
小さな共有記録は [measurements.json](measurements.json)、pathを含む詳細は生のmanifestにある。

## サイズの結果

単位はbyte。Flashは配置間隙を含むbinary長、RAMは本linkerの`.data`＋`.bss`。
差分は同じISAのbaselineに対する値。2runとも同じなので表は1組にまとめた。

| ISA/ABI | 構成 | Flash | 静的RAM | ΔFlash | ΔRAM |
|---|---|---:|---:|---:|---:|
| RV32EC/ILP32E | baseline | 24 | 4 | 0 | 0 |
| RV32EC/ILP32E | disabled_with_unused_object | 24 | 4 | 0 | 0 |
| RV32EC/ILP32E | disabled_with_unused_archive | 24 | 4 | 0 | 0 |
| RV32EC/ILP32E | weak_undefined | 38 | 4 | 14 | 0 |
| RV32EC/ILP32E | weak_archive_only | 38 | 4 | 14 | 0 |
| RV32EC/ILP32E | weak_empty_definition | 34 | 4 | 10 | 0 |
| RV32EC/ILP32E | weak_active_object | 76 | 72 | 52 | 68 |
| RV32EC/ILP32E | strong_active_archive | 70 | 72 | 46 | 68 |
| RV32EC/ILP32E | constructor_retained_object | 88 | 76 | 64 | 72 |
| RV32IMAC/ILP32 | baseline | 24 | 4 | 0 | 0 |
| RV32IMAC/ILP32 | disabled_with_unused_object | 24 | 4 | 0 | 0 |
| RV32IMAC/ILP32 | disabled_with_unused_archive | 24 | 4 | 0 | 0 |
| RV32IMAC/ILP32 | weak_undefined | 40 | 4 | 16 | 0 |
| RV32IMAC/ILP32 | weak_archive_only | 40 | 4 | 16 | 0 |
| RV32IMAC/ILP32 | weak_empty_definition | 36 | 4 | 12 | 0 |
| RV32IMAC/ILP32 | weak_active_object | 78 | 72 | 54 | 68 |
| RV32IMAC/ILP32 | strong_active_archive | 70 | 72 | 46 | 68 |
| RV32IMAC/ILP32 | constructor_retained_object | 88 | 76 | 64 | 72 |

constructor構成のFlash section和は86 byteで、binary長88 byteとの差2 byteは配置間隙。
section和と配置量を分けた集計になっている。

## 到達性と解釈

| 観測 | 証拠と意味 |
|---|---|
| disabled object／archiveは追加費用0 | service_hook・service_buffer・service_runsが最終symbolに存在せず、baselineとFlash・RAMが一致 |
| 未定義weakの呼び出し側には費用が残る | hookの実体はないが、逆アセンブルに存在確認・分岐・呼び出し側の命令が残る。追加14／16 byte |
| weak参照だけではarchiveを抽出しなかった | weak_archive_onlyはweak_undefinedと同じ数値。mapの抽出欄にservice.a(service.o)がなく、serviceのsymbolもない |
| strong参照ではarchiveを抽出 | map冒頭にentry2.oのservice_hookを満たすためのmember抽出を記録。serviceの関数・buffer・counterが存在 |
| constructorから未呼び出しserviceへ到達 | `.init_array`と登録関数・登録先pointerからserviceを保持。初期化表が保持されたことを検証 |
| 同条件の再現 | 全18行、symbolの住所・サイズ・種別、ELFとbinのSHA-256、出力先を置換したmapのhashが2runで一致 |

未使用ゼロを達成できたのは、この入力・flags・link方法の範囲。
弱い参照を置くだけで全費用が消えるわけではなく、constructor登録では呼ばない機能も保持し得ることが分かった。
これをD08のリンク条件の材料とし、実際の最小runtimeやA／B／Cの優劣はEXP-01以降で測る。

## 記録と失敗処理の検査

[記録入口](../../../../tests/platform/record_execution_link_cost.py)を追加し、元probeは変更していない。
各runを別出力先にし、ELFからsize・nm・objdump・objcopyを再実行して保存結果と照合する。
コマンドのstdout／stderr・return code、入力コピー・差分・tool識別、成功／失敗summaryとhash一覧を残す。

準備中には、内部helperの版取得とchecksumの接頭辞処理で記録スクリプトが3回失敗した。
いずれもbenchmark buildの開始前で、失敗を保存して修正後に新しい出力先で実行した。
cache archiveの実際のhashは正しく、checksum照合の不一致は記録側の表記処理の不具合だった。
準備失敗の記録も後述archiveに含めた。

次を実行し、11件成功。子プロセス失敗時のログ・return code、compiler不在時の失敗記録、
既存結果の上書き拒否、サイズ改ざんと2runの不一致を検出する検査。
既存pytest plugin由来の`record_xml_attribute` experimental warningが11件あり、失敗はない。

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/platform/test_execution_link_cost_record.py
```

## 再実行と証拠の移送

repository rootから次を実行する。outは未作成の名前に置き換える。

```sh
python3 tests/platform/record_execution_link_cost.py \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --out work/core-redesign-exp00-reproduce
```

今回の成功記録は `work/core-redesign-exp00-20261020-v4/`。
`manifest.json`に実行時点の入力hash・コピーとtoolを、`summary.json`に結果を、
`run1.audit.json`／`run2.audit.json`に到達性・成果物hashを保存した。
`sha256.json`が列挙する642ファイルをarchive内でも照合して一致を確認した。

移送用archiveは `work/core-redesign-exp00-20261020-evidence.tar.gz`、256,420 byte、
SHA-256は `8b30cedb92963a1a1ea1b34bfaf9318c6aa376d038616156932a85a773b414f5`。
`exp00/`に成功記録、`preparation-1/`〜`preparation-3/`に準備失敗、`shared/`に共有数値と検査ソースを含む。
compiler配布物自体は含めない。archiveと生ログはGit管理外で、共有する表・数値・手順はこのディレクトリに残す。

別作業領域では、次のどちらかで再生成できる。

1. この成果物を含むcheckoutを用意し、lockに従ってxPack toolを取得して上のコマンドを実行する。
2. 入力HEADのcheckoutにarchiveの`exp00/inputs/`の相対パスを重ねる。
   そのコピーに元probe・記録スクリプト・P1文書・tool定義が揃っている。
   `shared/test_execution_link_cost_record.py`は`tests/platform/`へ置く。
   toolを固定手順で用意し、新しいoutで実行する。archive内の旧コマンドの絶対pathをそのまま使わない。

toolが未取得の場合の入口は次。downloadはlockのchecksumで検証される。

```sh
uv run --no-project python tools/index/fetch_tools.py --tool xpack-riscv-none-elf-gcc
```

## 未検証と次の作業

stack・heap peak・cycle・遅延は測っておらず、JSONではnull。
実機起動、constructorの実行、IRQ、通信、Flashの物理操作、H417の適合性、教材評価もこの実験の対象ではない。
実機への接続・書き込みは行っていない。

次は通常モデルでP2の対象・入力を固定する。V003／X035の正確なpart・ABI・clock・配置・必要な周辺の資料、
K0のwire受理契約、X0のrenderer・入力・PDの分担を記録する。
その条件でhost参照モデルとR対照、上流との差、設備不足表を用意し、P3の候補比較へ渡す。
高位判断が必要な契約変更は今回発生していない。
