# N03 K0のIRQ共有・snapshot公開と通常進行

2026-10-21。実装commit `c4c0f11`。入力は [k0-shared-inputs.json](../../k0-shared-inputs.json)。
K0の受理容量・設定・status・snapshotの意味を保ち、割込み相当のfrontendと通常側処理の引渡しを試作した。
製品コアと既存の実験headerは変更していない。実行方式や公開APIの採用ではない。
N03の共有部分の結果であり、slave register backend／ACK対応とN03全体は未完了。

## 分割した責務

IRQ側は受信staging、完成設定FIFO、int16差分、sticky status、公開／保持用8 B snapshotを扱う。
各共有操作を短いgate scopeで保護し、IRQ方法からWorkExecutor／EventExecutorを呼ばない。
通常側の一回は次の順とする。

1. 保護下で完成設定を最大1件popし、差分をtakeして0にする。
2. 保護外で設定を検査し、適用・範囲飽和・出力byteの準備を行う。
3. 保護下で現在のsticky statusとmergeし、8 Bの公開値を更新する。

IRQのstatus更新を通常側の古い値で上書きせず、read開始の8 B保持copyと公開copyも同じgateで分ける。
実行器のturnとhandlerは所有するgate scopeの外。host policyはその条件をassertした。
このgateは単一コアのmaskable IRQ用の模型で、他core・NMI・DMAを止めるlockではない。

## 候補と比較条件

Rは専用の完成設定FIFO、Aは共通Queueを使い、通常側で明示処理する。
Bは同じ保護付きFIFOから処理するWorkExecutor、C-bridgeは同じFIFOと集約型の宛先通知を使う。
通常側は明示された進行機会ごとに共有状態を確認し、必要なら通常文脈からpost／notifyする。
M/S/U・user参加・禁止文脈は既存K0と同じ条件で検査したが、実loop／delayへ自動で接続してはいない。

**C-bridgeは追加条件であり、従来Cのframeを保持するEventExecutor FIFOとは異なる。**
IRQデータは保護付きFIFOに一度だけ保持し、eventはpayloadを運ばない。
既存K0-Cのsource・数値を置き換えず、同じ機能量の純粋な保護追加費用として順位付けしない。
使用中の汎用EventExecutorには未使用のpayload FIFO領域も残るため、最小化済みのbit通知器とは扱わない。

## 反例と保護条件

保護なしのnegative controlにも同じ処理と注入点を使った。

| 注入 | 保護なし | 保護あり |
|---|---|---|
| 差分1を読んでclearする前にIRQが+2 | 通常側が0を書いて+2を失い、最終position=1 | scope解除後に+2を保持し、次の進行後にposition=3 |
| 公開255→256の最初のbyte更新後にread開始 | snapshotに0が入り、255／256のどちらでもない | 公開copy完了後にread開始し256を保持 |

後者はbyte copy模型の反例で、実MCUで同じbyte順の命令が出る証明ではない。
targetの生成命令・実IRQ・物理的なpreemptionは別検証。

queue pop中の完成frame、差分take前後、通常計算中、公開各byte、公開後、idle確認中にも注入した。
IRQは模型の禁止中なら解除後に配達され、callbackを保護下でdispatchしなかった。
idle確認直後の到着ではB／C-bridgeが次の明示機会で処理する場合がある。
候補の内部callback回数や注入直後の途中positionを一律に揃えず、保持と後続進行を照合する。
これはsleep時のwake消失や、その後の明示呼出しなしでも進むことの保証ではない。

## 検査

最終native118件＋workspace3件、計121件成功。既存plugin由来warning242件。
165 trace、入力1,203,710操作を保存した。

- 既存の固定K trace13本、容量1／2、満杯・再試行、不正設定、read snapshot、M/S/Uを独立K0 oracleへ照合。
- 差分消失と混在snapshotのnegative controlをR/A/B/C-bridgeすべてで再現。
- 保護付きの公開8注入点、完成frameの引渡し、idle確認後の到着、入れ子と元から禁止の状態の復元を検査。
- seed 0〜9で各10,000回、通常+1とIRQ+1を各回与え、公開・takeの境界を選んだ。
  範囲を±32768付近へ広げ、最終position=20000、未回収差分0、IRQ10,000件を全候補で確認した。
  飽和による消失と競合による消失を混ぜず、最後は明示進行でdrainする。

固定の直列traceは独立oracle、IRQ境界はliteralと保存数のinvariantで確認した。
途中値やhostの内部呼出し回数の一致を、全候補へ要求した結果ではない。
一度に一件の注入模型であり、mask中の複数hardware eventやDR overrunを証明しない。

初回host runは109件成功の時点で、検査側の反復コマンド走査を一回へ直すため明示中断。
試作の動作は変えず、最終sourceで全件を再実行した。予備runを成功件数へ加算しない。
conftestは追加せず、remote CI成功とも扱わない。

## CSR policyとABI費用

[132構成](k0-shared-size-matrix.csv)は2 ABI×LTO有無で、4候補×容量1／2×保護有無×使用／未使用とemptyを比較した。
保護なしは反例用であり、正しさを満たす小さい候補として採用しない。
SDKの固定sourceをhash照合し、V003はmstatus（CSR 0x300）、X035はCSR 0x800のmask 0x88を保存・復元する。
今回はdisable後のfence.iを両方に含めた。既存EXP-02のV003 link policyは変更せず、同一とは仮定しない。
最初のABI runはzifenceiの指定不足でAssemblerが拒否したため保存し、
両比較群に同じ `_zifencei` を加えて再実行した。これはSDK使用命令への指定で、silicon検証ではない。

使用64行の逆アセンブルで、保護ありのCSR／fence.iと、保護なしでそれらがないことを確認。
SDKの単純enableを呼ぶ方式ではなく以前のmask bitsを復元するが、実IRQでの成立は未確認。

V003、容量1、LTOあり、単位B。括弧は保護なしnegative control。

| 候補 | Flash image | 静的RAM | 用途object | 最大単一frame |
|---|---:|---:|---:|---:|
| R | 1136（984） | 140 | 72 | 36 |
| A | 1088（944） | 140 | 72 | 32 |
| B | 1352（1176） | 180 | 108 | 40 |
| C-bridge | 1408（1232） | 188 | 120 | 36 |

68 Bの共通harness・初期値image・support関数を含む構造測定。
保護の有無によるRAM差はこのmatrixでは0 Bで、静的なmask管理objectを常設していない。
未使用64行は追加image／静的RAM／用途objectが0 B、empty binaryと一致。
allocator・constructor arrayは検出されなかった。.suの単一frameはstack peakではない。
vector・startup・slave backend・IRQの実stack・保存予約を含むB1成立や、禁止区間のcycleを測った結果ではない。

## 再現と残り

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_k0_shared.py tests/unit/test_workspace.py -q
python3 tests/platform/core_redesign/measure_k0_shared.py \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --out work/<new-k0-shared-abi>
```

`EXP06_K0_SHARED_EVIDENCE` は未作成directoryを指定し、入力・実行物・gzip traceを保存する。
[測定記録](k0-shared-measurements.json)にSDK参照・tool・hash・JUnit・trace・ABI・archiveを記録。
移送archiveは8,919,850 B、SHA-256 `35337f5582b93736525ac699cc4df24deec6f94a0d70c2cb52dcbdffce335506`。
次はslave資料から、受理／拒否とACK、STOP／repeated START、DR／snapshotの対応を固定してbackendへ接続する。
この論理共有の成功だけでK-HWや実wireを成立扱いにしない。[教材案](k0-shared-exercise.ja.md)は人の評価前。
