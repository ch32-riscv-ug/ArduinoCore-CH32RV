# unit

物理ボード不要の契約検査を置きます。現在の入口は test_workspace.py です。
通常の収集範囲、実機用ディレクトリの非実行性、旧 harness と依存環境の分離を確認します。
コア API の動作保証とは区別します。

純粋な Python 検査の隣へ物理ボード用 sketch を置きません。
host core を導入する場合も、その profile と必要 compiler を明示し、物理ボードなしで完結させます。
