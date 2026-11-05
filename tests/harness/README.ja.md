# harness

実機の試験の相手を決める補助の置き場です。DUT／peer の lifecycle は plugin 標準を使い、旧 harness は import しません。

- `place.py`: アドレス（`oep://<unit>/<口>[/<target>]`、`wchlink://<serial>`）から、プローブ、チップ、記録を読んで、試験の相手の事実と流す case を決める。
- `records.py`: プローブのメモ欄の記録（`oep.probe.memo/port/<口>`、`io.github.ch32-riscv-ug.arduino/port/<口>`）の名前、形、検査、読み書き。
- `bench.py`: run_hardware.py が知る case の名前。
- `provenance.py`: プローブの申告（firmware、インターフェース）の記録。

`../run_hardware.py` が隔離環境を準備し、plugin 標準の lifecycle へ渡します。`../bench_record.py` で記録を見たり書いたりします。
