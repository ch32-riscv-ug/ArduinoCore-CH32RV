# instrumented

DUT と外部刺激・波形・電圧計測を必要とする契約の配置先です。
`uart/`、`gpio/`、`power/` に独立UARTの往復、GPIOの両方向、PPPSの復帰契約があります。実配線と接続先はlocal設定に置き、[README](../README.ja.md)の明示入口から実行します。
