# sketch_support

新しい sketch の共通制御 protocol の配置先です。READY 問合せ、明示開始、安全な終了を扱います。旧 testcmd.h の採用は前提にしません。
`core_contracts/` のsketchは明示runnerがartifactディレクトリへstageして使用します。Pythonテストの保存先にはinoを置かず、収集時の自動uploadを防ぎます。

どの sketch も（console/ を除く。console の流れそのものが試験の対象なので）、起動すると `STARTUP <resetReasonName()>` を
1 行出します。試験の途中で DUT がリセットしたことを、「線に何も出ない」ではなく「DUT が再起動した」として報告するためです。
run_hardware.py は落ちた case の dut.log を見て、ある試験のログの 2 行目以降に STARTUP があれば、エラーの頭に
`the DUT restarted (<test>: <理由>)` を付け、result.json の `restarted` にも残します。1 行目の STARTUP は、コンソールが
開いた時に拾った起動なので数えません。わざとリセットする試験（system、flash）も、落ちたときだけ付くので邪魔になりません。
