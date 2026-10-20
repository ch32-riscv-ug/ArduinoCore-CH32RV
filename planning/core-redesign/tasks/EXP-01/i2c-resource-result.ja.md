# I2Cの寿命とclock・pin利用権の接続

2026-10-21。[有限I2C master](i2c-master-result.ja.md)を変更せず、直列な資源台帳とPortを別headerに追加した。
実装commitは`0eeae23`、進行停止時の追加検査は`a65964e`。
[入力](../../i2c-resource-inputs.json)、[数値・hash](i2c-resource-measurements.json)、[全28 ELF](i2c-resource-size-matrix.csv)を固定した。
製品コア・USB・Flashを変更していない。公開API・実行方式の採用はP5へ渡す。

## 接続する範囲

Domainは、宣言済みPCLK、clock変更中の状態、epochと、固定2枠の利用者台帳を持つ。
利用者はclockだけ、または周辺・SCL・SDAに相当する3 bitの資源をclaimできる。
bitは論理的な競合を表し、実GPIO番号・remap・pinmux registerを指定するものではない。
登録された利用者が1件でも残れば、clock変更の開始を拒否する。
clock変更開始後はPCLKを未確定にし、adapterが周波数をpublishするまで新しいclaimを拒否する。

PCLKは2～48 MHzの整数MHzという宣言値。publishは実RCC設定・測定ではない。
既存Clock48のHCLK確認から、PCLKが自動的に同じ値になると推定していない。
実clock adapterが周辺clockの設定・停止・復帰と整合させる作業は残る。
publishされない変更失敗からのrollbackやtimeoutも、この台帳は実装しない。

Portは、Domain・pin準備状態のadapter・既存Masterを接続する。
openでは周辺／SCL／SDAの3資源とclockをまとめてclaimし、pin準備の宣言を確認してからMasterをopenする。
pin未準備・不正速度・engine Busyで失敗した場合は、受理していない利用権を返す。
宣言が正しく、serializedな利用者だけが台帳を使うことが前提。新しいGPIO／RCC書込みは行わない。

Pin adapterの変更例も台帳のSCL／SDA利用権を確認して拒否する。
任意のpin adapterや別libraryが直接registerを変更するのを、Portが強制的に防止するものではない。
台帳に登録されない周辺、既に不明な状態でPEが立ったcontroller、raw MMIO、外部clock故障の安全性は保証しない。
openがBusyを返したことは、不明なhardwareを停止した証明にもならない。

## 利用権を保持する期間

| 状態 | buffer | clock／周辺／pin |
|---|---|---|
| open、要求なし | 利用者が使える | Portが保持 |
| 転送中・取消要求済み | Masterが借用 | Portが保持 |
| hardware終端、未回収 | Masterが借用 | Portが保持 |
| 通知ackだけ実行 | 借用継続 | Portが保持 |
| take後、まだopen | 利用者へ返却 | PEとtiming設定が残るため保持 |
| end済み、未終端／未回収 | 借用継続 | Portが保持 |
| end済み、終端をtake済み | 利用者へ返却 | 台帳へ返却 |

要求がないopen状態でendすれば、MasterがPEを無効にしてから返却する。
稼働中のendは取消要求であり、stepと終端回収を続ける。stop成立を待たずにpinやclockを返さない。
START／STOP待ちを何回pollしても、進行しなければ利用権が残る。自動timeoutやWDT処理を追加していない。
register模型での停止判定を使っており、線や実hardwareの静止を確認した証拠ではない。

clockだけの別利用者が残る場合、I2Cのend／takeが終わってもclock変更はできない。
利用権ticketは台帳内のslotと非周回generationを照合し、古い返却が同じslotの新しい利用者を解放しない。
clock epochも非周回。8 bit感度条件では枯渇で拒否し、再openやclock変更でgenerationを巻き戻さない。
ticketは同じ台帳の生存期間内で有効。別台帳や再構築後まで一意なcapabilityとは扱わない。
Port／buffer／台帳は回収まで生存させる。destructorで自動停止・返却はせず、ISR／thread共有の排他も提供していない。

## Host検査

ASan／UBSan、警告errorでnative47件＋workspace規則3件、計50件が通過した。
conftest.pyは追加していない。fixture内で既存driverから同じFake register部分だけを抽出し、生成sourceも保存した。

- 未確定PCLK、clock変更中、不正周波数、pin未準備、不正I2C速度、engine Busyとopen失敗後のclaim返却。
- 4 PCLK×100／400 kHz要求で、宣言値から既存engineが計算するregisterを照合。
- open中、転送中、hardware終端未回収、通知ack済み、take済みopen、end済み未回収の保持。
- read1／2／3／8／32 byte中のend、5種類のerror、古いtake、clock-only利用者、pin競合、2枠満杯、古い台帳ticket。
- STARTとSTOPで止め、100回のstep・clock変更要求・pin変更要求を繰り返しても早期解放しないこと。
- 4 seed×600台帳操作×8／32 bit counterを、独立したowner listと照合。台帳操作だけではI2C MMIOが発生しないことも確認。
- generationとepochの8 bit枯渇。

read／write／combined×4 seedの12条件では、裸の同じMasterとmanaged Portへ同じ正常進行を与えた。
register全値・アクセス順とcritical guard・address列・payload・結果を各操作で照合し、一致した。
同じengineの差分対照なので、それ自体でengine全体の正しさを証明した扱いにはしない。
以前のI2C試作59件とは別に、今回の接続範囲を確認したものである。

保存したmanaged traceは47本・7,810操作、対照traceは12本・914操作。
初回compileではOpen enumの名前がtemplate引数を隠すため拒否された。名前を修正した診断も保存し、最終成功とは分けた。

## 構造費用

GCC 14.3.0、`-Os`、V003 `rv32ec_zicsr/ilp32e`とX035 `rv32imac_zicsr/ilp32`、LTO off／onで28 ELF。
empty・engine単独・Domain単独・managed Port、およびそれぞれの未使用条件をbuildした。
独立startupはなく、GPIO／clock初期化・実通信・IRQ-safe backendを持たないlink probe。

両profile・両LTOで、Domain40 B、Master28 B、Port48 B（Master内包）、保護付き64 byte buffer68 B、pin宣言1 Bだった。
engine＋bufferは96 B、Domain＋Port＋buffer＋pin宣言は157 Bで、部品objectの差は61 B。
Domainは2利用者の共通台帳なので、接続する周辺ごとに必ず別の40 Bを複製するという設計ではない。
台帳の登録容量・範囲をどこまで一般化するかは未決定。

LTO onの入口・harness込み値。

| profile | engine image／static RAM B | Domain単独 B | managed image／static RAM B |
|---|---|---|---|
| V003 | 2,120／172 | 576／108 | 2,928／248 |
| X035 | 1,968／172 | 480／108 | 2,736／248 |

managedとengineのimage差はV003 808 B、X035 768 B、static RAM差は76 Bだった。
操作集合・harness・alignmentの差も含み、61 Bのobject差と同じものではない。
engine単独は資源競合を呼出し側に委ねるため、両者が同じ保護を提供する比較でもない。
以前の実startup付きI2C imageとも入口・buffer容量等が違い、そのまま増減比較しない。

未使用12条件はobject symbolと追加RAMが0 Bで、image hashは各emptyと一致。
全28 ELFでallocator symbol・constructor arrayを要求しない。
`.su`の単独frame値はCSVに保存した。IRQ・callee・libgcc込みのstack peakやCPU時間ではない。

## 再現と次の作業

```sh
EXP01_I2C_RESOURCE_EVIDENCE=work/i2c-resource-host-new \
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_i2c_resources.py tests/unit/test_workspace.py -q

python3 tests/platform/core_redesign/measure_i2c_resources.py \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --out work/i2c-resource-abi-new
```

保存archiveは`work/exp01-i2c-resource-20261021-evidence.tar.gz`。
source・生成Fake・compiler／binary hash・59 full trace・JUnit・28 ELFとmap／disasm／stack情報・初回診断・analysisを含む。
archiveのhash・byte数は測定JSONを参照する。出力先は新しいdirectoryを使う。

次は実PCLKとpinmux／remap adapter、clock変更失敗・喪失・停止／復帰の手順、ISRと資源引渡し・完了枠、固定arbiterと用途への接続を進める。
台帳を使わない利用者の扱い、回収不能なhardwareの終了方針、公開範囲は追加証拠とP5の検討に残す。
教材では「通知ackとtakeとendで何が返るか」「同じslotの古いticket」「なぜPCLK宣言と実clock確認が別なのか」を予想する。
人による教材実施・理解度レビューは未確認。
