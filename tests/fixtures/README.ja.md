# fixtures

共有するのは論理信号名、required capability、安全条件です。個体 ID、port、実配線、補正値は Git 管理外のローカル設定に分離します。
`bench.example.toml` を `bench.local.toml` へコピーして、このホストの接続先と配線を設定します。schemaと制約は `../harness/bench.py`、操作は[README](../README.ja.md)を参照してください。
