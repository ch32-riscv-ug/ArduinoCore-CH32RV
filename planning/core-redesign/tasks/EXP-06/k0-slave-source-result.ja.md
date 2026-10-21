# N03 K0 slave registerの対応調査

2026-10-21。共有模型の後続調査。実装commit `9574e6f`。
固定済みのWCH資料とSDKを再照合し、backendへ渡す条件と未確認点を整理した。
これは実機結果でも、wire契約の成立判定でもない。製品コアは変更していない。

## 一次資料と調査範囲

[CH32V003 RM V1.9](https://raw.githubusercontent.com/ch32-riscv-ug/CH32V003/12d9aed0eae0276a133db00eb16bd492e8189f2d/datasheet_en/CH32V003RM.PDF)
の13.4〜13.6、CTLR1、STAR1／STAR2を確認した。
資料中の印刷ページ150〜152、155〜160とPDFページ番号は異なる。機械記録はPDFの1始まり。
[WCH SDK I2C source](https://raw.githubusercontent.com/ch32-riscv-ug/CH32V003/12d9aed0eae0276a133db00eb16bd492e8189f2d/EVT/EXAM/SRC/Peripheral/src/ch32v00x_i2c.c)
のイベント取得・応答設定も確認した。
両fileのbyte数・SHA-256は既存[i2c-inputs.json](../../i2c-inputs.json)と一致する。
ミラー上の固定資料であり、現行の全revisionを網羅した調査ではない。

## 対応表

| 境界 | 資料から確認できること | backendの実装・検証条件 |
|---|---|---|
| アドレス一致 | 13.4ではアドレスへACKを返した後にADDRと割込みが立つ | ADDR割込みで満杯を判定しても、そのアドレスへのACKを後からNACKへ変えたとは言えない |
| ADDR解除 | STAR1→STAR2の読み出しで解除。EVT1は解除までSCL Lowを延長する | direction取得のSTAR2読み出しにも副作用がある。snapshot準備・ACK設定と解除の順序を実命令で確認する |
| 受信 | RxNEはDR未読。既存DRを読む前に次のbyteが来るとBTF、stretch有効時はSCL Lowを保持 | 一回の割込み＝一byteと仮定しない。最終byteとSTOPF、RXNEとBTFの同時状態を検査する |
| STOP | STAR1読出し→CTLR1書込みでSTOPF解除 | 残った受信byteを確定前に扱う。8 byteちょうどをSTOPで完成FIFOへ渡し、短い／長いframeは不正にする |
| ACK停止後のSTOP | STAR1のSTOPF説明にはACK=1の条件が付く | ACK=0で拒否した後も必ずSTOPFが来る、とする復帰コードを作らない。再設定時点と次のread／write可否を観測する |
| repeated START | STAR2のTRA等はrepeated STARTでclearされる。次の自アドレス一致はADDRで見える。SBはmasterの開始flag | 自アドレスへの再addressと、他アドレス・無応答への再STARTを区別する。汎用のslave START通知があると仮定しない |
| 送信 | TxEはDR空き。BTFはshift／DRの進行に関係し、EVT3_1／BTFはstretchを使う | TxEを「相手がそのbyteを読んだ件数」にしない。prefetch後のNACK、短いread、8 byte超過の0を検査する |
| readの終端 | 13.4のEVT3_2は相手のNACKでAF、STAR1へ0を書いて解除 | 正常なread終端を一律に通信失敗にしない。STOPFだけをread終了条件にしない |
| error | BERR／ARLO／OVRは別flag。stretch無効では受信欠落／送信重複があり得る | 一般error経路から未確定frameを完成扱いにしない。sticky設定不正との対応は実験契約として明記する |

SDKの`I2C_CheckEvent`（742行）と`I2C_GetLastEvent`（775行）はSTAR1→STAR2を読む。
したがって、この二関数を単なる「副作用のない状態取得」としてhandlerの先頭へ入れると、
ADDRが立っていた場合に準備前にstretchを解放する可能性がある。
固定SDKの実装から導く注意点であり、SDKの不具合と断定するものではない。

## まだ同一視できない受理方式

凍結K0は「満杯時に次のwrite取引を拒否する」。hostの`accepted=0`はwire上のNACKではない。
以下は比較候補の成立条件であり、採用・契約変更ではない。

| 対応候補 | 確認しなければならないこと |
|---|---|
| 満杯の間ACKを事前に止める | 次のreadアドレスも影響を受けないか。通常側の回収後、どの文脈で再開するか。拒否要求を検出できない間のstatus bit1をどう扱うか |
| writeのADDR後、最初のdataをNACKする | そのACK変更が間に合うか、RxNE／STOPFの挙動、ACKの再有効化と次のread。アドレスNACK方式とのpeer側の違い |
| ACKしながら取引全体を捨てる | 論理的拒否とwireで成功に見えることの差。status／sequence照合が必要であり、NACK済みとは記録しない |
| 完成枠が空くまでstretchする | 明示進行が止まると待ちが無期限になり得る。拒否ではなく待機への意味変更となるため、凍結K0の成立例へ混ぜない |

完成FIFOの枠とstagingは別。ADDRでの判断後に通常側が回収しても、その取引の
受理結果を途中で反転させない条件を保つ。IRQ内から通常実行器を進めて枠を空けない。

## 次のregister模型と実機で必要な刺激

| 刺激 | 確認対象 |
|---|---|
| 満杯→write address→0／1／8 byte→STOP→read→枠回収→再write | ACKの実位置、拒否status、復帰、readの可否。0 byte writeを正常設定へ数えない |
| 7／8／9 byteと、最終DR未読のままSTOPF | 残受信の排出順、正確な長さ、完成枠を一度だけ増やすこと |
| RXNE＋BTF、割込みを意図的に遅らせる | 2段の受信状態、欠落・OVR、SCL stretch。IRQ遅延をhost step時間と同一視しない |
| 8 byte→自アドレスreadへの再START | STOPなしの設定を適用せず不正にする。read snapshotは旧適用済み値 |
| 8 byte→別アドレス／無応答への再START→STOP | 再STARTが通知されない場合に、最後のSTOPで誤って8 byteを確定しないか |
| 0／1／7／8／9 byte read、途中の通常更新、NACK直後の再START | snapshot全体の固定、prefetch余分byteの隔離、超過0、次の取引のindex初期化 |
| ADDR＋旧RXNE／旧AF／STOPF、error＋最終byte | 旧取引の状態を新取引へ持ち越さない処理順。資料のflag規則から可能性を絞って模型化する |
| 100 kHz／400 kHz、gate保持中の受信／送信 | 実命令、IRQ優先度、stretch時間とpeerの許容。速度の成立は別々に判定 |

他アドレスへの再STARTについて、この資料だけでは常時観測できるとは確認できなかった。
これは「hardwareで不可能」という証明ではない。peer／bus構成による制限、追加の観測、
または契約上の扱いのどれが必要かを、未検証のまま残す。
通常のbackend実装ではまず資料で定義されたflagと副作用を模型化し、この条件を隠さず扱う。

## 保存・再現

```sh
python3 tests/platform/core_redesign/audit_k0_slave_sources.py \
  --source-dir work/exp01-i2c-sources-20261020 \
  --out work/<new-k0-slave-source>
```

source directoryには固定URLから得た上記2 fileを置く。scriptは通信せず、既存lockへ照合する。
PDF本文・抽出text・SDK・lock・script・位置情報を保存する。
機械的なsection検索は資料の位置確認だけで、上の意味解釈を自動検証したものではない。
[記録](k0-slave-source-measurements.json)に移送archiveとworkspace検査を記す。
workspaceは3件成功、既存warning6件。新たな用途native／ABI buildは0件。
共有模型の118件／132構成をこの調査の新規実行数へ加算しない。
実slave backend、wire観測、critical section時間、実stack、採否、人の評価は未完了。
