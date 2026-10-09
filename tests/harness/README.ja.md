# harness

設定解決と結果保存の最小限の補助の配置先です。DUT/peer lifecycle は plugin 標準を利用し、旧 harness は import しません。
`bench.py` は明示したlocal TOML、USB topologyとstable serial path、DUT identityを検証します。`../run_hardware.py` が隔離環境を準備し、plugin標準のlifecycleへ渡します。
