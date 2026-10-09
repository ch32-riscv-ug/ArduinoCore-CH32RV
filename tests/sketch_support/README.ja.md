# sketch_support

新しい sketch の共通制御 protocol の配置先です。READY 問合せ、明示開始、安全な終了を扱います。旧 testcmd.h の採用は前提にしません。
`core_contracts/` のsketchは明示runnerがartifactディレクトリへstageして使用します。Pythonテストの保存先にはinoを置かず、収集時の自動uploadを防ぎます。
