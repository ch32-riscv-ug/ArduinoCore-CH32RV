# UART なしの core bring-up

旧テストを使わない、ネイティブ Linux の明示実行用診断です。通常の pytest/CI から実機を操作しません。
接続は LinkE の RVSWD のみで、UART は使いません。OEP 経路はこの診断で保証しません。

確認する契約は .data/.bss 初期化、global constructor、setup/loop 到達、
DMSEQ の要求 R と応答、millis/micros/delay/delayMicroseconds の進行です。
時間 API は同じ DUT の時間基準による粗い自己検査で、周波数精度や独立した時刻基準の保証ではありません。
GPIO、外部割り込み、timer peripheral、ADC、UART、USB の動作は対象外です。

## 実行

固定依存を取得し、arduino-cli を PATH に用意します。

```sh
uv run --no-project python tools/index/fetch_tools.py --tool ch32rv
uv run --no-project python tools/index/fetch_tools.py --tool xpack-riscv-none-elf-gcc
```

作業ツリーのコアを隔離した Arduino user/data/downloads で build します。
既定は compile のみで、probe を開きません。出力先は新規ディレクトリを指定します。

```sh
uv run --no-project python tools/diagnostics/core-bringup/run.py \
  --chip CH32V205RCT6 --out work/bringup-build-v205
```

実機を書き換える場合だけ --execute と一意な serial: selector を指定します。
CH32X315MCU6 も同じ入口で別の --chip / --probe / --out を使います。

```sh
uv run --no-project python tools/diagnostics/core-bringup/run.py \
  --chip CH32V205RCT6 --probe serial:<ID> \
  --out work/bringup-v205 --execute
```

この selector はローカルの値であり、共有ソースに実個体 ID を保存しません。
診断中は他のツールから同じ probe/DUT を操作しないでください。
この入口の同時実行は selector 単位で排他しますが、他のアプリケーションとの全処理単位の排他を保証しません。

## 退避と復元

書込み前に SKU/verified、書込み容量、read/write protection を確認し、全 Flash を退避します。
V205 は 256 KiB。X315 は物理 480 KiB を退避しますが、書込みは zero-wait 192 KiB に限定します。
消去パターンは V205 が 0xffffffff、X315 が 0xe339e339 です。
非 blank の最後までを復元用 prefix とし、書込み可能領域を超える非 blank がある場合は、
復元できないので診断 firmware を書く前に中止します。

run の成否にかかわらず、書込み試行後は finally で chip erase と prefix の復元、
全容量の再読・byte 単位照合、option raw と UID の一致を検査します。
全容量をそのまま flash に渡して X315 の書込み制限を迂回しません。
強制終了・電源断では finally の復元を保証できません。

結果、compile/command/console ログ、ELF、元 Flash、復元 prefix、再読 Flash は出力先へ保存します。
result.json の restored が true でなければ次の試験をせず、保存した backup を保持して復旧します。
これらの個体情報・binary は Git 管理に含めません。probe firmware や option/protection 設定は変更しません。
