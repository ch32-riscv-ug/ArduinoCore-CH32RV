# unit

物理ボード不要の契約検査を置きます。現在の入口は test_workspace.py です。
通常の収集範囲、実機用ディレクトリの非実行性、旧 harness と依存環境の分離を確認します。
コア API の動作保証とは区別します。

純粋な Python 検査の隣へ物理ボード用 sketch を置きません。
host core を導入する場合も、その profile と必要 compiler を明示し、物理ボードなしで完結させます。

`test_example_serial_receivers.py`は、UART受信例の`.ino`をhost C++で直接実行し、
分割・連続受信、容量超過、checksum不一致、timeoutと時刻の桁あふれを確認します。
UARTの割り込み・受信バッファ容量や実際の配線は、この検査の対象ではありません。

`test_example_inputs.py`は、物理入力の5例をhost C++で実行し、チャタリング・押下中の起動、
時刻の桁あふれ、PWMへの換算、HID送信失敗後の解放・再接続、gamepadの校正と軸換算を確認します。
report型は実ライブラリを使いますが、USB転送・列挙・ADC・配線は実機確認が必要です。
