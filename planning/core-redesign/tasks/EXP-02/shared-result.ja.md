# EXP-02 型の寿命・保護・通知登録の比較結果

2026-10-20。入力HEADは `7b4a071`。製品APIや方式の採用を決めず、
[直列比較](result.ja.md)で残した契約を独立した試作で確認した。
EXP-02全体とP4までの作業は継続中。

## 比較した境界

| 試作 | 今回の契約 | 残る条件 |
|---|---|---|
| OwnedQueue | 固定placement storage、move-only、満杯ではmove／constructしない、pop後とclear／destructorで各要素を破棄。noexceptの型操作に限定 | 通常文脈専用。利用者のconstructor／destructorの時間・副作用を制限できない |
| GuardedChannel | gateで即時try操作を保護。Busy／Full／Emptyを区別。empty確認と通知登録を同じ保護区間で行う | gateが保証する文脈だけで利用。IRQ禁止はコア間lockにはならない |
| SpscChannel | producer1・consumer1、固定容量、全体copy、acquire／releaseでpayloadと再利用slotを公開 | 同じ側の再入不可。共有memory配置、cache、reset、実IRQとの適合は別検証 |

GuardedChannelの `prepare_wait()` は通知を消費してから条件を保護下で再確認する。
Readyなら処理を続け、Armedなら後のpushでstickyなpendingを立てる。
`take_wake()` は起動の観測だけで、データはpopして回収する。満杯で拒否した入力は送信側が保持する。
この接続はユーザーcallback、sleep、実行器、busy loopを暗黙に呼ばない。
実際の休止命令とwakeの競合、wait wrapperの進行条件はEXP-03／休止実験へ残す。

SPSCの順序付けはC++の[release／acquireの規則](https://eel.is/c++draft/atomics.order)に従う。
複数producerの試作はhostの `std::mutex::try_lock` をgateとして別に検査した。
これをISR用mutex、H417用lock、コアの待機APIとして採用した意味にはしない。

## Hostの証拠

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_shared_channels.py -o junit_family=xunit1
```

ASan／UBSan付き31件成功。既存pluginのexperimental warningは31件、検査失敗はない。

- 所有権：10 seed×20,000操作。独立deque、固定resource ledger、構築／破棄／解放数を照合。
  fullで入力が有効なまま残り、clearとscope終了でFIFO順に全解放する。64 byte整列型も確認。
- IRQ model：通知登録前、通知clear後、empty確認後、arm後、保護解除前の5点に到着を注入。
  入れ子と元から禁止の状態を保存し、保護区間の刺激を解除後に配達する。
  Readyまたはsticky wakeで到着を観測し、重複登録、満杯、再登録も検査。
- 複数producer：4 seed×4 producer×25,000件。consumerがproducer別FIFOと全payloadを照合。
  別threadのgate保持中は各操作がBusyとなり、出力値を変更しない。
- SPSC：容量1／4／64×4 seed×100,000件。sequence、反転値、checksumを照合。
  容量1／4は8 bit counterを使い、周回を繰り返す。空／満杯の拒否も確認。

ThreadSanitizerは別compileでMPSCとSPSC3容量をseed0で実行し、4条件とも終了0、診断出力なし。
呼び出し側の明示retryには `std::this_thread::yield()` を使うが、部品自身はretryしない。
有限回のstress成功は競合がないことの一般的な証明ではない。

## Target ABIでのリンク比較

```sh
python3 tests/platform/core_redesign/measure_data_parts.py --matrix shared \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --out work/exp02-shared-link
```

17ケース×2 ABI×LTO有無、68構成が成功。4種類の未使用object×4条件で、
追加Flash／静的RAM／init_arrayが0 byte、component／gate symbolなしを確認した。
OwnedQueueはraw storageへ明示構築する入口であり、非自明destructorを持つglobal objectの
自動登録まで無料になるという結果ではない。その起動・破棄契約はEXP-01で別比較する。

RV32E・LTOなし、16 byte payload・容量4の例。単位byte。

| ケース | Flash image | 静的RAM | Flash増分 | RAM増分 |
|---|---:|---:|---:|---:|
| baseline | 28 | 12 | 0 | 0 |
| 明示構築OwnedQueue | 336 | 84 | 308 | 72 |
| V003用CSR gate付きqueue | 508 | 92 | 480 | 80 |
| X035用CSR gate付きqueue | 556 | 92 | 528 | 80 |
| SPSC atomic queue | 264 | 84 | 236 | 72 |

volatile入口、全wordの観測、所有型の解放観測、libc／libgccを含む各プログラムの値。
型の意味と操作集合が異なるので、そのまま方式の順位にはしない。
全行は [shared-size-matrix.csv](shared-size-matrix.csv)、入力・tool・生成物hashは
[shared-measurements.json](shared-measurements.json)。既存serial matrixのsmoke8行は以前の容量値と一致した。

## IRQ資料と生成命令の限界

WCHの固定版SDKを照合した。
[V003 core_riscv.h](https://github.com/ch32-riscv-ug/CH32V003/blob/12d9aed0eae0276a133db00eb16bd492e8189f2d/EVT/EXAM/SRC/Core/core_riscv.h)の
enable／disableはmstatusの0x88を操作する。
[X035 core_riscv.h](https://github.com/ch32-riscv-ug/CH32X035/blob/327874484eda7e50cb4cf40271718e68fafbc850/EVT/EXAM/SRC/Core/core_riscv.h)は
CSR 0x800の0x88を操作し、disable後にfence.iを実行する。
実験gateはその差を分離し、csrrcで保存してから禁止、同じmaskだけを復元する。
compiler memory clobberを付け、保護中に呼び出し側がそのmaskを変更しない条件を置いた。
SDKの単純なdisable／enableだけでは、元の状態の復元を実装したことにはならない。

初回はX035のfence.iがISA指定不足でassemble失敗した。失敗ログを保持し、
shared matrixだけ両ABIの指定へ `zifencei` を追加した。serialの指定は変更していない。
両gateを両ABIでcompileする比較であり、異なる系列のgateをそのMCUで実行可能とするものではない。
容量4 SPSCの4構成では生成されたfenceを確認し、__atomic helperへの呼び出しはなかった。
これは当該tool・型・配置での命令観測で、実チップのbarrier／cache適合の証明ではない。

実IRQのpending／nested／復帰、禁止時間、clockとstack、H417両コアからの観測は未検証。
実機backendとして確定する前にチップ資料と実機で残りを検証する。

## 記録と継続先

hostは `work/exp02-shared-host-20261020/`、最終matrixは
`work/exp02-shared-link-20261020-final/`。入力コピー、command／exit／stdout／stderr、
ELF／map／disasm／symbol／section／.su／hash一覧を保存した。
移送archiveは `work/exp02-shared-20261020-evidence.tar.gz`、897,321 byte、
SHA-256 `36bdcdc5f89e9dfc6e53be717049df351a028eeae50c32d44f74c96071cb899b`。
一次資料と最初の失敗も含む。Gitには小さい集計と再実行用のsourceを残す。

次はEXP-03の実行器へtry部品・起動通知を接続し、有限turn、再投稿、timer、cancel、waitの
進行条件を比較する。取得buffer、ring／message／poolの実共有、実機とmulticoreは未完了として維持する。
