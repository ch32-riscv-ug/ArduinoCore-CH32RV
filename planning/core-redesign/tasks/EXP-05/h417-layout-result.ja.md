# H417両imageの共有領域：固定SDK linkerでの配置検査

2026-10-21。CORE-01のlink検査。[入力](../../h417-layout-inputs.json)と[全command・ELF情報・hash](h417-layout-measurements.json)を固定した。
実装commitは`fd994af`、ABI判定の補強は`9fe0f89`。製品コア・USB・Flashを変更せず、書込み・実起動も行っていない。
実行方式・公開APIの採用はP5へ渡す。

## 置いたものと使ったlinker

[一次資料調査](h417-preparation.ja.md)で固定したSDKの
[V3F linker](https://github.com/openwch/ch32h417/blob/6d1e469a5e9874a67613721ad7fd47b9de96654f/EVT/EXAM/CPU/HSEM/HSEM_DataSharing/Common/Ld/V3F/Link_v3f.ld)と
[V5F linker](https://github.com/openwch/ch32h417/blob/6d1e469a5e9874a67613721ad7fd47b9de96654f/EVT/EXAM/CPU/HSEM/HSEM_DataSharing/Common/Ld/V5F/Link_v5f.ld)を、hash照合して無変更で使う。
V3F用にcompileした同じ`shared.o`を、V5Fは元scriptの`INPUT ../../V3F/obj/User/shared.o`で取り込む。
SDK本体のstartupやdriverはlinkしていない。

共有objectは16 byte alignmentのvolatile byte配列だけ。
256 B・1 KiB・32 KiBを別条件にし、参照・lock・constructor・公開／回収protocolを持たせない。
256 Bは既存所有部品の236 Bを置ける程度の配置予算であり、その部品を構築したものではない。
16 byte alignmentも実験上の境界で、H417のDMA／cache要件を確定した値ではない。

各imageには初期値付きprivate word、13 wordのBSS、link symbolを参照する最小の入口を置く。
その入口はRAM初期化・vector・IRQ・hardware stack・他core起動を実装しておらず、実行可能なH417 firmwareとして配布するものではない。
GCC 14.3.0のgeneric `rv32imafc_zicsr/ilp32f`を使い、vendor拡張命令や実startupの成立は検査しない。

## 予約と配置

範囲の終端は含まない。privateは「このlinkerでそのimageに割り当てる」の意味で、他coreからのアクセスを禁止するhardware保護の意味ではない。

| 領域 | V3F | V5F |
|---|---|---|
| Flash予約 | `0x00000000–0x00010000` | `0x00010000–0x00030000` |
| code用RAM予約 | `0x20100000–0x20110000` | `0x200A0000–0x200C0000` |
| load用RAM予約 | `0x20110000–0x20110100` | `0x200C0200–0x200C0300` |
| 通常RAM予約 | `0x20110100–0x20178000` | `0x200C0300–0x20100000` |
| shared予約 | `0x20178000–0x20180000` | 同じ32 KiB |
| stack | `0x20177800–0x20178000` | `0x200FF800–0x20100000` |

3容量×LTO off／onの6 pair、12 ELFを照合した。
全条件で`.shared`のaddress・size・alignment、`experiment_shared` symbolのaddress・size、両imageのABI flagsが今回の指定値3（RVC＋single-float ABI）に一致した。
`.shared`はwritable／allocatedのNOBITS。通常の`.data`／`.bss`／`.stack`とは重ならない。

各imageの`.data`は8 B、BSSは52 B、stack予約は2,048 Bだった。
`_data_vma–_edata`、`_sbss–_ebss`、`_susrstack–_eusrstack`をsectionと照合し、sharedを含まないことを確認した。
BSS終端からstack予約までのgapはV3F 423,620 B、V5F 259,268 B。
これは小さいlink probeの空きであり、完成用途やSDK全体を載せた場合の空き容量ではない。
code／load用RAMには実startupのcopy対象を置いておらず、その動作やアクセス権限も検査していない。

重要な費用の区別として、**byte配列が256 Bでも、SDK scriptは32 KiB全体を通常RAMから除外する**。
section使用量と予約容量は別。以前の小型ABIでの「未使用object追加0 B」を、この固定H417 linkerの予約容量0 Bという意味に読み替えない。
sharedを使わない場合の予約除去と、容量選択を両imageへ一致させる設定は次の検討対象。

## 検査が拒否した条件

実linkでは次の3条件が想定通り失敗した。

- sharedに32 KiB＋16 Bを置く：`RAM_SHARED`が16 B超過。
- V3Fのprivate BSSを通常RAM容量以上にする：RAM超過とstack重複。
- V5Fのprivate BSSを通常RAM容量以上にする：RAM超過とstack重複。

board-free検査は配置判定36件＋workspace規則3件の計39件が通過した。conftest.pyは追加していない。
配置模型ではsharedのaddress・size・NOBITS属性・flags・alignment・symbol形状、ABI、MEMORY予約、ctor、
BSS／data load／stack境界、private／sharedの重複、未知のallocated section、予約の重複、entry等を壊して拒否を確認する。
両方が同じRVE flagやRVCなしflagになる誤設定も拒否する。
ELF decoderでもtruncation・誤class／machine・table範囲・section名／file範囲・不正alignment・segment sizeを拒否する。
これらは小さいELF32検査器の対象条件で、一般的なELF形式全体やすべてのloaderを検証するものではない。

## NOLOADと初期化・uploadの境界

実ELFのsharedには、`p_filesz=0`、`p_memsz=配列の容量`のLOAD segmentが残る。
Flashからprogramするbyteは含まれないが、loaderがmemory size分をzero化するかどうかは別条件。
NOLOADやNOBITSというsection属性だけで、2本目のimageをuploadしたときに共有状態を保持できると決めない。
実upload／debug loaderの経路と、全chip reset・片側の起動／再起動を分けて調べる必要がある。

今回は初期化symbolの範囲だけを照合した。
実startupがどの順序でcopy／clearするか、初期化ownerと他coreのhandshake、旧DMA／旧coreの静止を証明していない。
byte配列はconstructorを持たないため、NOLOADへC++ objectを置いた場合の寿命・初期化の扱いも別途必要。

[前の所有部品](h417-features-result.ja.md)にはgateへのreferenceがある。
この配置成功から、そのobjectをそのまま共有RAMへ置いて両coreで使えるとは結論しない。
共有表現をpointerなしにするか、coreごとのview／gateをどう接続するか、ABI・起動世代・再構築の条件を決めてから接続する。

## 再現と次の作業

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_h417_layout.py tests/unit/test_workspace.py -q

python3 tests/platform/core_redesign/build_h417_layout.py \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --bundle work/exp05-h417-research-20261021 \
  --out work/h417-layout-new
```

bundleには固定manifestに一致するSDK linkerのcopyが必要。同じ出力directoryは再利用しない。
保存archiveは`work/exp05-h417-layout-20261021-evidence-r3.tar.gz`。
source、固定linker、compiler／binutils hash、commandと失敗診断、ELF／map／symbol／segment／disasm、JUnit、analysisを含む。
archiveのhash・byte数は測定JSONを参照する。

次のP4作業は共有予約の容量／未使用設定、共有表現とcore-local view、初期化handshake・実行器接続・共有実行器比較。
[容量と未使用の派生link実験](h417-reservation-result.ja.md)は追加で行った。公開設定・実起動への採用は未決定。
実upload・backend・可視性・DMA・片側resetは未検証。
教材では「使用256 Bと予約32 KiBの違い」「NOBITSなら2本目のuploadで保持されるか」「gate参照を共有してよいか」を先に予測する。
人による教材実施・理解度レビューは未確認。
