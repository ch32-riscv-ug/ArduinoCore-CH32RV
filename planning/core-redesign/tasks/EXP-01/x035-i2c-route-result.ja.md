# X035のI2C routeとpackage能力差

2026-10-21。[default routeの準備](x035-i2c-setup-result.ja.md)を残し、別headerでrouteとpackageを型へ指定する比較を追加した。
実装commitは`71984ad`。[入力](../../x035-i2c-route-inputs.json)、[数値・hash](x035-i2c-route-measurements.json)、[68 ELFの表](x035-i2c-route-size-matrix.csv)を保存した。
製品コア・USB・Flash実装と以前の試作は変更していない。公開APIや対応方式の採用はP5へ渡す。

## 固定したrouteと対象package

根拠は[WCH RM V1.9](https://www.wch-ic.com/download/file?id=382)のAFIO PCFR1、印刷p71／73と、
[DS V2.3](https://www.wch-ic.com/download/file?id=381)のC8／F7 pin図、印刷p11（PDF13）。
sourceの版・hashは[前の入力](../../x035-i2c-setup-inputs.json)を引き継ぐ。
DS図は画像でも確認済み。C8はLQFP48、F7はTSSOP20で、今回この2 packageだけを比較する。

| I2C remap値 | SCL | SDA | GPIO設定register／mask | C8 | F7 | 別機能との関係 |
|---:|---|---|---|---|---|---|
| 0 | PA10 | PA11 | GPIOA CFGHR／`0x0000ff00` | 有 | 無 | default |
| 1 | PA13 | PA14 | GPIOA CFGHR／`0x0ff00000` | 有 | 無 | ほかのalternate機能と競合し得る |
| 2 | PC16 | PC17 | GPIOC CFGXR／`0x000000ff` | 有 | 有 | USB data pin兼用 |
| 3 | PC19 | PC18 | GPIOC CFGXR／`0x0000ff00` | 有 | 有 | debug pin兼用 |
| 4 | PC17 | PC16 | GPIOC CFGXR／`0x000000ff` | 有 | 有 | USB data pin兼用 |
| 5 | PC18 | PC19 | GPIOC CFGXR／`0x0000ff00` | 有 | 有 | debug pin兼用 |
| 6 | PC17 | PC16 | GPIOC CFGXR／`0x000000ff` | 有 | 有 | 4と同じpin mapping |
| 7 | PC18 | PC19 | GPIOC CFGXR／`0x0000ff00` | 有 | 有 | 5と同じpin mapping |

RMは4／6を`1x0`、5／7を`1x1`として説明する。
今回のreadback判定では書いた3 bitをそのまま照合しており、aliasの実hardware readbackは未確認。
GPIOの対象nibbleは前と同じ9（AF push-pull、I2Cでは自動open-drain）。routeはcompile時に固定し、runtimeのpin tableやroute dispatcherをtargetへ常設しない。

F7にはPA10～PA14が出ていないため、route0／1はprepareの最初にUnsupportedPackageを返す。
claim・MMIO前に拒否し、readyもfalseになる。存在しないpinへ設定writeして成功扱いにしない。
このguardは未対応packageをすべて安全に扱う汎用検出器ではなく、資料を確認した2種類の能力表である。

## debugとUSBの扱い

debug pinを使うroute3／5／7は、AFIOのSW_CFGが`100`になっていることを追加条件とした。
既存ownerがdebugを解放した状態を要求し、このadapter自身はSW_CFGを書き換えない。
未解放ならgate有効化後にFaultHeldとなり、I2C remapとGPIO設定は行わない。
準備後にSW_CFGを含む状態が変わっていれば、open時の再確認でも拒否する。

USB data pinを使うroute2／4／6については、USB controllerの所有やenable状態を検査するbackendを追加していない。
cold／quiescentの外部宣言には、選んだpinをほかの機能が使用していないことも必要となる。
Domainの3 bitは周辺／SCL／SDAの論理的な競合であり、全GPIOとUSB／debug／PIOC等を横断する物理pin台帳ではない。
register設定が通ることを、USBと同時利用できるという証拠にしない。USB stackの変更も行っていない。

## Host検査

ASan／UBSan・警告errorでnative136件＋workspace規則3件、計139件が通過した。conftest.pyは追加していない。
136 trace、1,057操作を保存した。型ごとのhost binaryは2 package×8 routeの16本。
以前のI2C Fakeとresource driverを再利用し、GPIOC CFGXRのclock-gate検査を模型へ追加した。
派生C++とFake全文、入力・compiler・binary hashも保存する。

- C8 route0では、以前の100シナリオとliteral assertionを再利用。旧観測へprojectionし、既存test moduleは変更しない。
- 有効な14組合せで、対象以外のGPIO register・nibble・remap fieldを保持し、3 byte受信とend／takeを確認。
- F7 route0／1はprepare／ready／openの拒否を確認。MMIOを行わず、利用権も保持しない。
- 6つのdebug route／package組合せで、SW_CFG未解放ならFaultHeld、宣言なしのabandonは拒否、外部で解放した後に準備できることを確認。
- 14組合せで、準備後に別routeへ変更するとopen時に拒否し、I2C engineのwriteを始めないことを確認。

I2C registerとpayloadの模型であり、SCL／SDAが実pinに出ることや、pinの電気的動作を再現するものではない。
準備と利用権の引渡し、実clock精度、IRQ共有等の制限は前の準備adapterと同じ。

## ABIと能力guardの費用

GCC14.3.0、`-Os`、`rv32imac_zicsr/ilp32`、LTO off／on。
C8とF7の各packageにempty・8 route使用・8 route未使用を作り、68 ELFをlinkした。
CPU／Flash／RAMの構造profileを共通化し、packageに応じた起動・board設定を実装したfirmwareとはしない。
両partの62 KiB Flash・20 KiB RAMは固定DSの値を使う。

LTO onのimage B。各使用構成のstatic RAMは260 B。

| route | C8 image B | F7 image B |
|---:|---:|---:|
| 0 | 3,280 | 2,584（準備拒否） |
| 1 | 3,288 | 2,584（準備拒否） |
| 2 | 3,272 | 3,272 |
| 3 | 3,320 | 3,320 |
| 4 | 3,272 | 3,272 |
| 5 | 3,320 | 3,320 |
| 6 | 3,272 | 3,272 |
| 7 | 3,320 | 3,320 |

Domain40 B、Port48 B、buffer68 B、Setup16 Bで、routeに応じたobject増加はなかった。
C8 route0は、LTO off／onとも前のdefault専用試作とimage byte hash・static RAMが一致した。
この条件ではroute選択を追加してもdefault構成のcode費用は増えなかった。
debug guardなどを持つrouteとの差は入口・生成codeも含み、実行時間の差ではない。

F7 route0／1は安全に拒否するが、PortとDomainのAPIも呼ぶこの構成では管理objectが残る。
UnsupportedPackageという結果を返すことと、機能をまったくinstantiateしないことを同じ0 Bとして扱わない。
非対応APIを公開しないか、stubを提供するか、その場合どのstorageまで除くかは未決定。

未使用32条件は追加RAM・object symbolが0 Bで、image hashもそれぞれのemptyと一致した。
全68 ELFでallocator symbol・constructor arrayはない。
単独stack frameはCSVへ保存し、call chain／IRQ込みのpeakや実時間は測定していない。

## 再現と残る接続

```sh
EXP01_X035_ROUTE_EVIDENCE=work/x035-i2c-route-host-new \
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_x035_i2c_routes.py tests/unit/test_workspace.py -q

python3 tests/platform/core_redesign/measure_x035_i2c_routes.py \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --out work/x035-i2c-route-abi-new
```

保存archiveは`work/exp01-x035-i2c-route-20261021-evidence.tar.gz`。
一次資料・pin図・source・派生driverとFake・16 host binaryと全trace・JUnit・68 ELFとmap／disasm／stack情報・analysisを含む。
byte数・SHA256は測定JSONを参照する。出力先には新しいdirectoryを使う。

次のP4作業は物理pin競合と他周辺の所有、IRQ共有、clock喪失／停止復帰、完了枠・arbiterと用途への接続。
実配線・bus・USB／debugとの共存・電流は未検証。
教材では「同じ系列でもpackageでpinが違う」「I2C routeが選べることとpinが空いていることの違い」「拒否stubと未使用除去」を予想する。
人による教材実施・理解度レビューは未確認。
