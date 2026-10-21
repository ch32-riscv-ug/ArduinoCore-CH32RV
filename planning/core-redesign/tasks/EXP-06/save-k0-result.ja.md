# N05 V003 K0と保存の共存

2026-10-22。実験実装 `3772773`。最終仕様・保存API・公開wire契約の採用ではない。
[固定入力](../../save-k0-inputs.json)、[測定manifest](save-k0-measurements.json)、
[ABI表](save-k0-size-matrix.csv)、[変更課題](save-k0-exercise.ja.md)。
production core／USB／Network／並行の永続化仕様下書きは変更していない。

## 接続条件

[N03 K0共有](k0-shared-result.ja.md)と[条件付きslave register](k0-slave-register-result.ja.md)へ、
N05の `SaveCoexistence` を接続した。R／A／B／C-bridge、完了設定FIFO1件、
ACK-discard／data-NACK＋明示rearmの2条件を比較する。C-bridgeは固定N03の通知条件で、従来のframe配送Cと同じではない。
Gate／Registersは既存driverから無改変で切り出す。媒体模型はX035共存driverから切り出し、
page幾何の3式だけ256→64に変更する。故障maskのsentinel256は変更しない。
元fixtureの2 byte RX、TX先読み、STOPF条件、foreign再STARTの制限を引き継ぐ。

保存領域は0／1／2／4 fast erase単位、各64 B。ここでの「1単位」は64 B fast eraseで、
標準eraseの1 KiB sectorと混同しない。payloadは位置の符号付き32 bit表現＋caller tagの8 B。
1単位は1 bank上書き、2／4単位は2 bank、別footerなし。履歴・key公開APIを選んだものではない。
requestは位置をその場でコピーし媒体を読まない。progressは最大32 B走査、または同期erase／program1回。
結果はtakeまで保持し、take後に明示progressで再走査する。reboot模型では媒体を残し、
アプリ・周辺・入力源・clock・台帳を作り直す。保存位置をアプリへ自動復元しない。

## 時間と通信の模型

`docs/flash-raw.ja.md` のcommit `bbe0ee7`を参照snapshotとして保存した。
V003F4U6 24 MHzのsampleはerase64 B 3,130 us、program64 B 2,810 us、buffer load86 us。
これを感度分析の入力にする。target linkのF4P6と同一実測条件だったとは扱わず、上限保証にもしない。
loadはtick停止区間の外、erase／programは通常アプリとFlash上のIRQ処理が進まない区間とする。
wall-derived msと、停止区間につき最大1 pending tickを加算する観測msを分ける。

仮想外部入力源は1 msごとの±1、保持容量1／4件。保存中はsourceだけが進み、明示deliverでK0のdeltaへ渡す。
K0の16 bit蓄積値や設定FIFOの容量とは別物で、実GPIOに4件FIFOがあるという主張ではない。
I2C peerは自動では進めず、write／byte／consume／stopを明示する。
ADDR／RX2件／OVR／TX先読みが保留された状態を作り、保存中にport・FIFO・snapshotが変わらないこと、
明示irq後の回収またはFaultを検査する。実ストレッチ時間、peer timeout、Flash停止中のshift動作は未検証。
`run`はadvance→deliver→irq→application→save progressの実験ループであり、採用runtimeではない。

## 検査結果

最終ASan／UBSan runはnative93件＋workspace3件、失敗・skipなし。376 trace・60,300コマンド、
全4候補を含む94組で各コマンド後の全出力が一致した。

- 0／1／2／4単位でmount、負の位置の即時capture、未回収による再要求拒否、takeと再走査を検査。
- 2 clock×2入力容量×4保存容量で、停止時間・入力欠落・clockの差を検査。
- 2 slave policy×4保留状態と、正しい設定frame／保持readを保存と組み合わせた。
- 3容量×5 erase故障、3容量×3 program故障、3容量×4 cold-model boot境界を検査。
- 10 seed×120選択の設定・入力・保存・回収・再走査・rebootで候補間を比較。
- 保存無効時は既存slave driverを別binaryとして実行し、4候補×2 policyで意味・frontend・port・公開snapshot・MMIO件数を照合。
- 生の媒体dumpをPythonの構造体／CRC decoderで独立に読み、C++ scanner出力と比較。unstable read故障は安定したdumpと同一視しない。

これはP1の全用途乱数予算を満たした宣言ではない。N08で不足を横断確認する。

100 ms wall、quantum1 ms、50 ms後にsave requestを1回発行した条件は以下。
返却後の次の明示deliverまでの入力も欠落数へ含む。実IRQの回数ではない。

| 保存単位 | 保持1件の欠落 | 保持4件の欠落 | wall由来ms | 1 pending tick模型ms |
|---|---:|---:|---:|---:|
| 0 | 0 | 0 | 100 | 100 |
| 1 | 6 | 0 | 100 | 96 |
| 2 | 6 | 0 | 100 | 96 |
| 4 | 9 | 0 | 100 | 94 |

4単位は1 bankが2 pageとなりeraseが2回になる。この固定blob8では保存履歴は増えない。
容量増加を自動的な保護・性能向上として扱えない。

1 bankで旧recordを消した後にprogramが失敗すれば、旧値は残らない。
erase失敗もmask次第で旧値維持／Empty／Dirtyになる。2 bankではこの模型の非active bank故障中も旧値が残る。
programがfalseでも全byteまたは先頭半分を反映すると、64 B pageの前半にheader＋blob8が収まり新recordが有効になる。
falseを「新値が絶対に見えない」と解釈できない。CRC・故障maskは任意の物理電断に対する保証ではない。

## ターゲット向け費用

V003 RV32E、LTOなし／あり、4候補×2 policy×使用4容量＋未使用各1構成とempty、計82 buildが成功。
使用64行、未使用16行。最終policy0の42 imageは先行42 buildと一致した。
未使用16行はemptyと同じimage、アプリ・保存・backendの静的RAMと予約はいずれも0 B。
未使用型の容量指定だけで、利用者が明示確保した予約を自動返却するという意味ではない。

以下はR／ACK-discard／LTOあり。harness68 Bとport1 B等を含む全静的RAMと、個別objectを区別する。

| fast erase単位 | image B | 静的RAM B | K0 object B | 保存object B | opaque backend B | Flash予約 B | 最大単一.su B |
|---|---:|---:|---:|---:|---:|---:|---:|
| 0 | 1,744 | 144 | 72 | 0 | 0 | 0 | 32 |
| 1 | 3,992 | 364 | 72 | 208 | 12 | 64 | 48 |
| 2 | 4,048 | 364 | 72 | 208 | 12 | 128 | 48 |
| 4 | 4,048 | 364 | 72 | 208 | 12 | 256 | 48 |

保存の静的RAM差は220 B。この汎用store／所有／分割scanの費用は、64 Bを直接読むだけの専用実装との最小差ではない。
Flash末尾のNOLOAD予約、input data混入拒否、imageの重なり拒否を検査した。独立した負のlink2件も意図どおり拒否された。
linkerはzero alias、媒体契約は0x08000000基準であり、実backendでのアドレス変換・upload保持は別途必要。
保存backendは既存opaque hookをno LTOでlinkし、実Flash制御命令を実装しない。
slaveは既存MMIO・IRQ gateへの構造的linkで、clock／pin設定・vector・reset／実IRQを備えた実行firmwareではない。
単一.suはpeak stackではなく、2 KiB RAMに全runtimeが収まった証拠でもない。

## 再現と履歴

最終host：`work/exp06-save-k0-host-20261022-r6`、XML／log：`work/exp06-save-k0-tests-20261022-r6.*`。
最終ABI：`work/exp06-save-k0-abi-20261022-r3`。分析と負のlink：`work/exp06-save-k0-analysis-20261022`。
実行コマンド・tool／input hash・binary・trace・linker・ELF・逆アセンブルをarchiveへ含めた。

初回はshellにpythonがなく入力JSON作成を失敗し76 setup error、r2は混在auto宣言のcompile errorで76 setup error。
r3は72成功・7失敗で、Empty／Dirtyの誤った期待2件と、NACK後に再armせず送信する無効刺激5件を修正。
r4は79成功。追加したboot・旧fixture比較・通常loop検査後のr5は94成功・2失敗で、保持4件でも欠落するとした期待を修正。
fixture本体を期待に合わせて変更せず、最終r6で96件成功。初回ABIは1 build後にfence.iのISA宣言不足で停止し、
既存N03と同じzifencei宣言を付けたr2で42成功、2 policy比較のr3で82成功。失敗履歴も保存した。

## 残る境界

X035とV003の同期媒体に対するN05 host／link比較を実施した。
実Flash backend・RAM配置・起動／vector・時刻接続・全体fitはN08、実ACK・IRQ停止・電断・upload保持・peakはN09で扱う。
非同期媒体、追記・整理、寿命・wear、別消去geometryはこの共存結果では未検証。
別計画の `docs/persistence-spec.ja.md` は採用未定の並行仕様であり、本実験から変更・採用しない。
次の独立作業はN06の休止・複数コアと実行器の接続。方式の採否はP5へ渡す。
