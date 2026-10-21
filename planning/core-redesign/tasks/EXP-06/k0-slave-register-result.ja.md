# N03 条件付きslave register adapterとK0用途

2026-10-21。実装commit `6733394`。入力は[k0-slave-register-inputs.json](../../k0-slave-register-inputs.json)。
[共有部分](k0-shared-result.ja.md)の同じfrontendへ、副作用付きregister模型とtarget MMIO入口を接続した。
[資料対応](k0-slave-source-result.ja.md)にある不明点を前提条件として残し、実wireの成立や採用は宣言しない。
製品コア・既存実験headerは変更していない。

## 接続した範囲

R／A／B／C-bridge、完成FIFO容量1／2に、同じ`K0SlaveRegister`を接続した。
ISR相当の入口は共有frontendだけを呼び、実行器は明示的な通常側opportunityで進める。
割込み一回の処理は受信最大2 byte、送信最大1 byte。処理時間の上限を測定した意味ではない。

- 旧取引の受信DRを先に排出し、STOPを処理してから次のADDRを扱う。
- ADDRのSTAR2読出しでstretchが解放される前にsnapshotを保持する。方向未確定なのでwriteアドレスでも未使用のsnapshot copyを行う。
- TxEでDRへ先読みし、peerが読んだbyte数とは区別する。read末尾のAFは通常終端として処理する。
- BERR／ARLO／OVRでは未確定frameを捨て、PE／ACKを止める。復帰はbus idleを確認する明示操作。clock／pin等の準備は外部条件。
- registerのerror clearは対象bitへ0、他bitへ1を書き、error flagのread-modify-writeによる取りこぼしを避ける形にした。実周辺での成立は未確認。

エラー時の設定不正bitへの対応、明示復帰、二つの拒否方式は実験内の規則で、公開APIの採用ではない。
adapterに渡す容量はApplicationの完成FIFO容量と一致させる。製品向けの型契約を確定したものではない。

## 二つの拒否対応

| 条件 | ACKして捨てる | dataをNACKする |
|---|---|---|
| 満杯のwrite address | 既にACK済み | 既にACK済み |
| write data | ACKするが設定を保持しない | 受信ACKを止める。NACKされたbyteのRXNE有無を両方試験 |
| sticky拒否／完成FIFO | 拒否を記録し、旧完成設定を維持 | 同左 |
| peerからの見え方 | write成功に見える。status／適用sequenceの照合が必要 | data NACKとして見えるという模型条件 |
| 次のread | 開始できる | ACK停止中はread addressもNACK。STOP後の明示rearmが必要 |

data-NACKでは、完成FIFOが満杯ならSTAR2でdirectionを読む前に一時的にACKを止め、
transmitter方向なら直後にACKを戻す。readをwrite拒否としてstatusへ数えない。
writeならACK=0を保持し、模型ではSTOPFを期待しない。bus idleになった後の明示rearmで戻す。
回収でFIFOが空いても、bus busy中のrearmは成功にしない。
この順序の物理ACK、address認識、STOP検出、次の取引への成立は実測が必要。

二方式はpeerに見える意味が異なる。同じwire仕様を実装した方式のサイズ順位にはしない。
IRQ内から実行器を進めたり、空き枠を待って無期限にstretchしたりする経路は含めていない。

## 検査と反例

最終native137件＋workspace3件、計140件成功。既存plugin由来warning280件。
207 trace・298,863コマンドを保存し、同じ刺激を全4群へ与えた51組の全公開状態とMMIO記録が一致した。
比較には意味の違う拒否policyやforeign STOP模型を混ぜない。

1. 長さ0／7／8／9、二段受信、最終DR＋STOPF、旧最終DR＋新ADDR、STOPF＋新ADDRを検査。
2. 容量1／2の満杯・拒否・read・回収・再送、data NACK後のbusy／idle rearmを検査。
3. 1／7／8／9／12 byte read、DR先読み、途中の通常更新、AF＋次ADDR、超過0を検査。
4. 自アドレスへの再STARTでは8 byte stagingも不正として破棄。完成した設定はSTOPを経たものだけ。
5. BERR／ARLO／OVRと複合error、busy中の復帰拒否、idle後の復帰を検査。
6. M／S／Uで通常処理の進行を検査。IRQだけで設定適用や差分回収が進まないことを確認。
7. seed 0〜9、各1,000反復の直列設定／入力を独立の凍結KnobOracleへ照合。比較点は取引と通常stepの境界。

全interleavingを網羅した結果ではない。実時間もなく、IRQ共有の注入検査は先の共有結果を参照する。
P1の乱数条件をこの1,000反復だけで充足したとは扱わず、N08で条件を横断整理する。

二種類のnegative controlも保存した。

- SDK式STAR1→STAR2取得をadapterより先に行うと、模型のADDRが消え、準備前にstretchを解除し送信開始を失う。
- 他アドレスへの再STARTが通知されない場合、後でSTOPFが来る模型では8 byte設定を誤確定する。来ない模型ではstagingを保持し続ける。どちらも必要な不正statusを記録できない。

後者の二模型は未確認点を示すための代替仮定であり、実siliconの動作を断定しない。
正の比較は再addressが自アドレスに限られるfixture条件。
この制限を一般的な共有busの保証や、K-HWの全契約を満たした結果に拡大しない。

初回は84件成功・32件失敗。失敗はSTOP解除後の確認読出しも禁止していた検査条件による。
必要な読書き順序の検査へ修正し、24条件を追加して最終140件を再実行した。
adapter／driverの動作はその間不変。初回の件数を最終成功数へ加算しない。

## 構造費用

[132構成](k0-slave-register-size-matrix.csv)：V003 RV32E／X035 RV32I、LTO有無、4候補、容量1／2、
2拒否policy、使用／未使用とempty。未使用64行は追加image／RAM／両objectが0 B、empty binaryと一致。
allocator・constructor arrayは検出されなかった。使用64行でCSR／fence.iとI2C MMIO baseを含む逆アセンブルを確認した。
offset／baseは既存I2C probeと同じで、今回の入口は実vectorや周辺準備へつないでいない。

V003、LTOあり、容量1、単位B。各欄はACK-discard／data-NACK。

| 候補 | Flash image | 静的RAM | 用途object | adapter object | 最大単一frame |
|---|---:|---:|---:|---:|---:|
| R | 1664／1816 | 144 | 72 | 1 | 32 |
| A | 1624／1776 | 144 | 72 | 1 | 32 |
| B | 1864／1928 | 184 | 108 | 1 | 40 |
| C-bridge | 1920／1984 | 192 | 120 | 1 | 44 |

共通harness68 Bと整列を含む。adapterの状態は1 Bだが、静的RAM総量には整列が影響する。
共有単体probeとは入口・操作・観測が異なるため、image差を純粋なbackend追加費用にはしない。
C-bridgeは先の結果と同じ追加通知条件で、凍結K0-Cのframe保持event FIFOとは別。
最大単一frameは`.su`の値で、呼出し全体やIRQ stackのpeakではない。
startup／vector／clock・pin準備／実ISR／永続領域を含む用途全体のfitではない。

## 再現と残作業

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_k0_slave_register.py tests/unit/test_workspace.py -q
python3 tests/platform/core_redesign/measure_k0_slave_register.py \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --out work/<new-k0-slave-abi>
```

`EXP06_K0_SLAVE_EVIDENCE`へ未作成directoryを指定するとsource／実行物／traceを保存する。
[測定記録](k0-slave-register-measurements.json)に初回失敗、最終JUnit、入力hash、ABI、archiveを保存する。
[教材案](k0-slave-register-exercise.ja.md)は人による評価前。
N03のhost／MMIO link範囲を追加した。実IRQ、拒否後復帰、他アドレス再START、wire上のACK、
100／400 kHz、時間・stackは未検証。N09の実機依頼とN10の契約不足へ残し、最終方式はP5で判断する。
次の独立作業はN04の待機・再入深さと別stack対照の確認。
