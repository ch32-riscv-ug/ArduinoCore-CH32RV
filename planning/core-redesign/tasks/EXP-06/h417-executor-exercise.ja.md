# 公開・hint・所有を区別する課題案

[N06直列接続結果](h417-executor-result.ja.md)を使う。利用者レビューは未実施。

1. messageをpushしてturnだけを繰り返し、notify後と比較する。dataの公開、wake hint、handler実行を別の操作として説明する。
2. empty確認直後に公開を注入する。clear-beforeとlate-clearで共有queueとadapter hintがどうなるかを予測する。
   hintを失うこととpayloadを失うことを区別し、明示hintで再走査する。
3. blockをtakeして8 byteずつ読み、途中でworkerを止める。2 blockが埋まったときのreserveを確認し、
   最後のreadとreleaseのどちらで生成側へ容量が戻るかを説明する。
4. read途中でrecoverし、worker停止とDMA quietの宣言を段階的にattestする。古いticket／leaseが新しい公開を操作できないことを確認する。
   online=falseが実CPUやDMA停止の証拠にならない理由を挙げる。
5. Gateをbusyにしてturnする。bool falseをemptyと判断した場合に不足する情報と、
   SerialGate.enteredを並列のcall-local結果として使えない理由を説明する。
6. F0〜F3のABI表で、アプリobject、harness、RAM、Flashの違いを読む。LTOで不変配列がFlashへ移る例を確認する。
   両core共通のobjectへcore-local executor／pointerをそのまま置けない理由を説明する。

次の並列接続では、容量・公開順序・停止／復旧条件を変えず、追加の保護と状態の費用を比較する。
人の評価、編集差分と追加状態の統一集計は別工程。
