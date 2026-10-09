# tests

[テスト計画](TEST_PLAN.ja.md)と[カバレッジ](../docs/test-coverage.ja.md)に従い、通常の検査と、接続先を明示する実機契約を分けます。旧suiteと設定はimportしません。

## ボード不要の検査

```sh
cd tests
uv sync --locked
uv run --locked pytest
```

repository rootからは `uv run --project tests --locked pytest -c tests/pyproject.toml` です。通常の収集範囲は `unit/` と `platform/` で、Arduinoへの書込み、monitor、電源操作をしません。`platform/` は旧 `build/` の名前を変更したもので、固定sourceのオフライン整合検査を置きます。生成物のディレクトリではありません。

## 実機契約

ネイティブLinuxで固定toolを取得し、[bench.example.toml](fixtures/bench.example.toml)を `fixtures/bench.local.toml` へコピーして接続先を埋めます。個体ID、USB topology、実配線と共通ロックはGit管理外です。接続情報はこの明示的な設定だけから読み、別のリポジトリや隣接checkoutは検索しません。以下はrepository rootで実行します。

`arduino-cli` をPATHへ配置し、serial／HIDへのアクセス権を準備します。power caseにはPPPS対応ハブ、`uhubctl`、USBとsysfs port disable属性の権限が必要です。

```sh
uv run --no-project python tools/index/fetch_tools.py --tool ch32rv
uv run --no-project python tools/index/fetch_tools.py --tool xpack-riscv-none-elf-gcc
uv sync --project tests --locked --extra hardware

# 接続先を開かず、対応するsketchをcompile（outは未作成のディレクトリ）
uv run --project tests --locked --extra hardware python tests/run_hardware.py \
  --bench tests/fixtures/bench.local.toml --case runtime --out work/runtime-compile

# 個体照合、書込み、観測、宣言したcaseの実行
uv run --project tests --locked --extra hardware python tests/run_hardware.py \
  --bench tests/fixtures/bench.local.toml --execute --out work/hardware-run
```

[.env.example](.env.example)を `tests/.env` へコピーすれば、設備TOMLと既存の共通ロック、結果の親ディレクトリを設定できます。設定値は `uv --env-file` で明示的に読み込みます。CLIの `--bench`、`--lock`、`--out` が環境変数より優先します。`--out` を省略すると `OEP_HW_RESULTS` 内に実行ごとの新しいディレクトリを作ります。書込みには常に `--execute` が必要です。

```sh
uv run --project tests --locked --extra hardware --env-file tests/.env \
  python tests/run_hardware.py --target configured-target --case runtime --execute
```

OEPでは `probe_unit_id` に完全な個体IDを設定してください。専用OEP USBの `usb_serial` からも照合できます。USB-UART bridgeを使う場合は、そのserialとOEP個体IDを別々に設定します。プローブの個体照合に失敗した場合、DUTの操作には進みません。共通ロックは設備管理側で作成済みのファイルを指定し、テスト側では作成・コピーしません。

`--target <名前>` と `--case runtime|uart|gpio|hid|power` は繰り返し指定できます。省略時はlocal設定に明記したtargetと、そのtargetが宣言したcaseだけを実行します。未配線caseの明示要求は設定エラーで、skipによる成功にはしません。

| case / contract | 必要設備と確認するもの | 確認しないもの |
|---|---|---|
| runtime / runtime.init、runtime.time-progress、runtime.print-heap、console.round-trip | DUTとDMSEQ対応プローブ。毎回のbuild ID＋challenge応答、data/bss/constructor、Print/String/malloc、時間APIの粗い進行 | OOM・断片化、時間精度、全clock条件・wrap境界、全console方式 |
| uart / uart.external-round-trip | 標準UARTに接続した独立UART。115200 8N1、64 byte binaryの往復 | 他instance/route/baud/format、overflow、波形精度 |
| gpio / gpio.external-levels | OEP GPIOと指定DUT pad。出力の外部観測と外部入力刺激、終了時INPUT | 未配線pad、全package、analog/PWM/interrupt |
| hid / upload.hid-runtime | UIAPduino V003、ソフトUSB、OEP SWIO＋NRST。HID権限、Arduino upload recipe、独立DMSEQ上の新build IDとruntime | Arduino USB stack、USB PD、bootloader領域の書換え |
| power / usb.port-cycle | 指定PPPSハブ。対象だけのUSB消失、再列挙、個体照合、DUT再attach | VDDが0 Vまで落ちること、DUTのcold boot証明 |

現在の配線ではLink＋V103、LinkE＋V307、ESP32＋V003、RP2350＋L103、追加のLinkE＋V003を対象にできます。L103と追加のLinkE＋V003にUART配線は宣言していません。GPIOはESP32側のV003に設定したpadだけです。P4フル結線、USB対向機、未接続系列は配線・契約の確定後に追加します。

UIAPduino V003は `upload_port = "hid://<soft_usb_topology>"` を設定します。runtime／UART／GPIOでも毎回SWIO＋NRSTからbootloaderへ移行し、HIDで書き込み、ESP32上のDMSEQとfixtureで観測します。RP2350は `upload_port = "oep://<usb_serial>/<slot>"` を設定します。console用のserial pathとupload先をそれぞれ指定します。

## 実行とartifact

`run_hardware.py` は設定・環境の準備を行い、compile/upload/monitorとDUT lifecycleは固定したpytest pluginへ渡します。Pythonテストはsketchと別に保管し、実行時にout内へ組み立てるため、テスト収集だけでuploadは始まりません。Arduinoのuser/data/downloads、platform.local.txt、sketch、生成物、ログはout内に作り、作業ツリーのコアを使用します。通常のsketchbookや `.arduino15` は変更しません。

out内のArduino CLI wrapperはupload時のdiscovery timeoutを10秒にします。HID／OEPのcustom discoveryをCLI既定の1秒で打ち切らないためです。HID profileには `protocol: hid` も指定します。

実機時は共通ロックを全期間保持し、stable serial pathとUSB topology／VID／PID／serialを照合します。Link側はDUT UID、現在のOEP target.infoがUIDを返さない経路ではchip IDとSKUを照合します。OEPのchip IDは系列・構成の識別で、DUT個体のUID保証とは区別します。

Flashの退避は行わず、指定したテストsketchを書き込んで使用します。接続先情報、コアcommit/dirty、tool版、build ID、コマンド、pytestログ、DUTログ、JUnit、`result.json` を保存します。caseごとの失敗を記録して後続targetも処理します。

テスト後はテストsketchがDUTに残ります。自動復元は行いません。V003のsystem bootloader領域は操作しません。PPPSのOFF区間はfinallyでONへ戻し、GPIOはINPUTへ解放します。中断時はartifactと機材状態を確認してから再開してください。

固定Python依存は `pyproject.toml` の `hardware` extraと `uv.lock` で管理します。GPIO／UARTのplanは試験用session内で扱い、プローブfirmwareの更新や保存設定の変更は行いません。

### プローブ版と責務

`result.json` の `host` は実際のArduino CLI・ch32rv（DB/stub digestを含む）・Python・plugin/client版とuv.lockのhashを記録します。`probes.<target>` と `preflight/<target>/probe.json` はプローブの申告版・model・個体情報を記録します。WCHはraw／正規化版／WCH表記とknown_badを、OEPはfirmware文字列をそのまま保存し、protocol revision、interface revisionとdescribe宣言も保存します。ホストclientの版とプローブfirmwareの版は別項目です。取得失敗・版なしは `metadata_status` とエラー／nullで明示し、推測で埋めません。compile-onlyではプローブ情報を取得しません。

このコアではプローブfirmwareの数値的な最低版を一律に指定しません。ケースの実行に必要な機能・interface revisionの互換性はch32rv／OEP client／pluginの判定と実行結果に従います。版やcapabilityの記録だけで互換性をPASSにしません。プローブ側の既知不良・対応条件・更新imageの検証はプローブ提供側、host依存の固定はこのsuiteのlockで管理します。新しい最低版が必要な不具合を見つけたときは、根拠となる契約と修正を所有リポジトリへ渡します。

## 配置

| 場所 | 内容 |
|---|---|
| unit/ | 設定、identity、電源対象、収集範囲、保守ツールのボード不要検査 |
| platform/ | 固定sourceの整合検査。toolchainを使うplatform契約の配置先 |
| single/runtime/ | DUT runtimeとconsoleの契約 |
| instrumented/uart/、gpio/、power/ | 独立UART、GPIO刺激・観測、USB電源復帰 |
| sketch_support/core_contracts/ | 実行時にstageするsketch |
| fixtures/、harness/ | 設定雛形と最小限の設定・identity照合 |
| loopback/、peer/、manual/、diagnostics/ | 今後の契約の配置先 |

full compile matrix、startup等価性、size baseline、未配線周辺機能は、このsuiteの成功で検証済みとは扱いません。
