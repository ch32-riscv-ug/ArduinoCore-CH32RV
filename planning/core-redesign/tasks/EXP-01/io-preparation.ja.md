# EXP-01準備：I/Oの所有・終端・取消model

2026-10-20。入力HEAD `3b693b9`。IO-01〜04を検査するための独立した通常直列文脈のmodelを作った。
既存のR用途対照とは分離しており、物理I2C／DMA／IRQ、最小起動、完成コアの費用は未検証。
このmodelの成功でEXP-01全体を完了にしない。

## 所有と終端

`IoSlot` はbackendの受理記録。閉じたfrontendの状態より長く存在できる記録として扱い、
借りた `IoBuffer` と共にhardware停止と結果回収まで生存させる。
bufferの利用者向けread／writeは貸出中に拒否する。raw pointerを保持する利用者への保護や、
任意のC++ objectの破棄を自動で安全にする仕組みではない。

| 操作 | この実験での意味 |
|---|---|
| start | open済み、空きslot、対応mode、有効buffer／長さ／addressのときだけ受理。Accepted／Busy／Invalid／Unsupported／Closed／Exhaustedを区別 |
| cancel | 停止要求の受理。要求済み、終端済み、古いtokenを区別。要求しただけでは終端にもbuffer返却にもならない |
| hardware_terminal | fake hardwareが全アクセス停止を証言する境界。成功／失敗／取消の最初の終端だけ採用。成功は全長、失敗／取消は部分転送を許す |
| peek／ack notice | stickyな起動通知の観察と受理確認。ackしても終端結果とbufferは保持 |
| take | 正しい世代の終端を一度だけ回収し、その時にbufferを返却 |
| end | 新規受理と通常通知を停止。実行中なら取消要求。既存の終端と借用を勝手に消去しない |

取消要求後に通常の成功・失敗が先に終端になった場合、その結果を維持する。
取消の終端は事前に停止要求したときだけ受理する。二度目の終端は結果や受信byteを変更しない。
readの部分失敗では実際に転送したprefixだけを書き換える。
終端後も回収するまでslotはBusyで、bufferを別slotへ貸し直せない。

end後のhardware終端は記録するが、通常通知を発生させない。
close／reopenで世代を初期化せず、古いrequestの終端・取消・回収・通知ackを新しいrequestへ適用しない。
最小8 bit世代で上限に達したslotは退役する。
addressはこのmodelで7 bit範囲だけを確認し、実busのaddress制約・予約値・波形まで再現した意味にはしない。

## Host証拠

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_io_lifecycle.py -o junit_family=xunit1
```

ASan／UBSan付き22件成功。既存plugin由来のexperimental warning22件。
固定ケースは受理拒否の優先順位、成功／失敗／取消と停止要求の両順序、取消受理後のbuffer再利用拒否、
endと終端の両順序、reopenと古い世代、部分受信、二重終端・二重回収、8 bit世代の退役を含む。
失敗したread／take／peekは出力値を変更しないことも比較する。

10 seed×20,000操作をPythonの独立した取引・所有台帳へ照合した。
2 slotと2 bufferを使い、利用者のread／write、別slotへの二重貸出、通知保留、
停止要求とhardwareの遷移を入力ごとに観測する。
hardware停止の物理的な証明、IRQが割り込む文脈、通常queueへの配達接続はこの台帳の対象外。
特に `hardware_terminal()` を呼べるというだけでは、実際のDMA停止を証明しない。

## Target ABIでの参考link

```sh
python3 tests/platform/core_redesign/measure_data_parts.py --matrix io \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --out work/exp01-io-link
```

12ケース×2 ABI×LTO有無、48構成が成功。
bufferだけ、slot＋buffer、2組のslot＋bufferを8／32／128 byteで作った。
未使用8条件で追加Flash／静的RAM／init_arrayが0 byte、buffer／slot symbolなしを確認。

RV32E・LTOなしの例。単位byte。

| model入口 | Flash image | 静的RAM | Flash増分 | RAM増分 |
|---|---:|---:|---:|---:|
| baseline | 30 | 40 | 0 | 0 |
| 32 byte保護bufferだけ | 96 | 76 | 66 | 36 |
| 32 byte buffer＋slot | 976 | 108 | 946 | 68 |
| 同2組 | 1092 | 176 | 1062 | 136 |

この値は所有検査、受信模擬、転送byteのFNV観測、volatile操作入口を含むmodelの参考値。
診断を外した製品候補の費用やI2C driver単独の費用として順位付けしない。
初回はhostでunsignedと同じ型だったTokenをtargetでもunsigned参照へ渡してcompile失敗した。
target側のuint32_tは別の基底型だったため、入口を `IoSlot::Token` に修正し、失敗ログを保持した。
ABIでcompileする検査がhostのみの検査を補った例であり、物理動作の確認ではない。

全行は [io-size-matrix.csv](io-size-matrix.csv)、入力・tool・ELF hash等は
[io-measurements.json](io-measurements.json)。
hostは `work/exp01-io-host-20261020/`、最終linkは `work/exp01-io-link-20261020-final/`。
入力コピー・command・終了値・stdout／stderr・map・symbol・disasm・section・.suを保存して照合した。
移送archive `work/exp01-io-20261020-evidence.tar.gz`、670,082 byte、
SHA-256 `d3fe363460b9209b25942556ae7220fb635a3ed65c04714144300f3e87912fe3`。初回のcompile失敗も含む。

## 継続する作業

このmodelを共通seamとして、EXP-03のwork／宛先通知へ接続し、満杯時でも終端を失わない条件を確認する。
通知が配達済みでもendした世代へcallbackしない接続を、dispatcher側で追加検査する。
raw借用／型付き所有、完了枠予約、arbiterは別の費用比較として扱う。
EXP-01の最小runtime、GPIO・時間・I2Cの物理backend、定数／実行時入口、低水準への資源引渡しは残る。

## 独立runtimeへの後続

[reset／GPIO入口](runtime-result.ja.md)に、IRQ無効の独立ELF、data／bss・constructor呼出しの命令検査、
定数／実行時pinのrawとclass比較を記録した。実起動やI/O取消・停止を検証した意味ではない。
clock・時刻源・I2C、ISR入口、所有modelとの接続は引き続き別の作業として残す。
