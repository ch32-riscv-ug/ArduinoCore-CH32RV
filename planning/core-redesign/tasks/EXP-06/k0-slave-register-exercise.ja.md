# K0 slaveの受理とpeer側の見え方を追う

対象は[条件付き模型](k0-slave-register-result.ja.md)。人の理解度・変更成績は未評価。
製品向けサンプルや採用済みAPIではない。

1. 完成FIFOを一杯にし、次のwriteとreadを行う。
   ACKして捨てる方式とdata NACK方式について、peerが知れる結果、status、適用sequenceを予測する。
2. data NACKの後、通常処理で完成設定を回収するがbusはbusyのままにする。
   rearmが失敗する理由と、idle後にどの明示呼出しが必要かを説明する。
3. 7 byteを受信し、8 byte目がDRに残ったままSTOPFが立つ刺激を作る。
   完成FIFOへの引渡しより先に必要な操作を予測し、traceで確認する。
4. 8 byte設定の後、STOPを使わずreadへ再STARTする。
   適用sequence、status、read snapshotを予測する。
5. read先読みを2 byte進めた状態で、peerは1 byteだけ読みNACKする。
   `read_index`をpeerの実消費数として使えない理由を説明する。
6. 他アドレスへの再STARTの反例を比較する。
   観測していない信号から正しい境界を推定できるか、fixture制限と一般APIの保証を区別する。

各課題は、予測→実行trace→差の説明を記録する。未観測のACK時刻や実IRQ時間をhost結果から埋めない。
N07の5変更課題の実施・差分集計とは別の教材素材。
