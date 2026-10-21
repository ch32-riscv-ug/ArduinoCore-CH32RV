# I2Cの回収分離と資源利用権の接続

2026-10-21。実装commit `80d83a0`。
[入力](../../i2c-managed-handoff-inputs.json)、[全28 ELF](i2c-managed-handoff-size-matrix.csv)、
[host・tool・hash・比較台帳](i2c-managed-handoff-measurements.json)を保存した。
[I2C回収分離](i2c-handoff-result.ja.md)と[資源台帳](i2c-resource-result.ja.md)のheaderを変更せず、外側のManagedへ接続した。
製品コア・USB・Networkは編集していない。これは通常文脈で直列に呼ぶhost／ABI実験で、公開APIや方式の採用決定ではない。

## 保持期間を接続する

ManagedはCore Managerと、現在保持するDomainへのpointer・Ticketを持つ。
open_backendで論理資源のperipheral／SCL／SDAを表すmask 7をclaimし、その後にPins.readyを確認し、Domainが宣言するPCLKを既存Masterへ渡す。
clock変更中・未宣言・競合・台帳世代枯渇ではclaimを拒否する。失敗したclaimではPinsもMMIOも呼ばない。
Pins未準備・不正速度・MasterのBusyでは、今回取得したleaseを返却する。
既にopenしている場合は再取得や速度変更を行わない。

| 状態 | backendのclose | clock／pinのlease | 外側のresult／buffer |
|---|---|---|---|
| open、要求なし | 許可 | closeまで保持 | なし |
| Queued | 拒否 | 保持 | 受理時から借用 |
| Running、取消要求後を含む | 拒否 | 保持 | 借用継続 |
| MasterがTerminal、reap前 | 拒否 | 保持 | 外側はRunningのまま |
| 明示reap後、他のQueued／Runningなし | 許可 | closeで返却 | Terminal結果とbufferは保持 |
| 利用者take | backend状態を変更しない | 現在の他ownerにも触れない | 当該結果を回収しbufferを返却 |

client end、cancel、reap、notice、ack、takeからclock／pinを自動返却しない。
利用者takeを待たずcloseできる一方、reapしただけでは周辺がopenしているためclock変更はまだ拒否する。
closeも暗黙にQueuedを取り消したりRunningを待ったりしない。
明示step・dispatchの呼出しがなければ進行せず、時刻保証や自動retryはない。

別のManagedが同じDomainを使用する場合、先のownerのclose後にPCLK宣言を変更してopenできる。
旧ownerの外側結果とbufferはその間も保持される。新ownerの通信中に旧ownerのtakeを呼んでも、新ownerのleaseを返さない。
外側bufferを新ownerへ二重に貸すsubmitは、旧ownerのtake前には拒否される。
このtokenは呼出し先Managerの生存期間内だけの識別子で、別instanceやbootをまたぐ権限識別子ではない。

## Hostで確認したこと

ASan／UBSan、`-Wall -Wextra -Werror`でnative74件＋workspace3件、計77件が通過した。
新規74 trace・5,426操作、基準34 trace・2,486操作を保存した。
専用3 record／共有2 record、各client未回収1要求、外部buffer64 B×3を使う。
両方式とも利用者は3で、共有方式の未受理clientに対する公平性を追加したものではない。

- read長1／2／3／4／8／32／64、write、tx＋readの34条件で、元の回収分離Managerと全共通出力が一致した。MMIO・register・送信列・ADDR時control・完了prefix・bufferも含む。
- CheckedBusは全MMIOとcritical enter／leaveで、当該Managedがleaseを保持し、Domainのmaskが7であることをassertする。Pins.readyもclaim後だけ呼ばれる。
- Queued取消／client end、Runningの3段階でのend、停止後reap前、idle openで、closeとclock変更の境界を検査した。
- 旧結果の保持中に資源を返却し、48 MHz／400 kHzから12 MHz／100 kHzへ宣言を変更して別ownerが通信する。その途中に旧ownerのtakeと重複closeを実行しても、新ownerのleaseとbyteを壊さない。
- 未宣言・変更中、Pins未準備、不正速度、PE残存、wire BUSYのopen拒否とrollbackを検査した。48 MHzから2 MHzへ変更後、400 kHzを拒否して100 kHzで再openできる。
- 他ownerのmask 1／2／4／7はopenを阻止する。mask 0のclock-only leaseはI2Cと併存できるが、I2C close後もclock変更を阻止する。
- 5種のerror flagについて、停止／reap／close後も失敗prefixを保持する。別ownerのlease取得後に旧結果をtakeしてもそのleaseは残る。
- 8 bitの台帳ticketは255回のopen／closeで非周回退役し、以後のclaimをMMIOなしで拒否する。clock epochの255回到達も最後の宣言値を保持して変更を拒否する。

全traceの最後で外側bufferを回収した。
元のMaster／Fake／回収分離Manager／Domainとprotocol helperのhashは従来の記録と一致する。
専用／共有の基準入力はargsが違うため、それぞれ17種類、計34 unique traceである。
fixtureはtest module内に置き、conftest.pyは追加していない。

## 費用の測定

V003 rv32ec／ilp32eとX035 rv32imac／ilp32、LTO off／onで28 ELFをlinkした。
回収分離Managerのみの基準8 imageは、以前の記録とhash・サイズ・RAM・最大単体frameが一致した。
LTOありの構造観測用imageは次のとおり。

| 対象 | record方式 | 基準image B | 接続後image B | 差 B | 基準RAM B | 接続後RAM B | 差 B |
|---|---|---:|---:|---:|---:|---:|---:|
| V003 | 専用3 | 3,336 | 3,808 | 472 | 456 | 508 | 52 |
| V003 | 共有2 | 3,360 | 3,832 | 472 | 420 | 472 | 52 |
| X035 | 専用3 | 3,072 | 3,552 | 480 | 456 | 508 | 52 |
| X035 | 共有2 | 3,112 | 3,576 | 464 | 420 | 472 | 52 |

Managed本体は専用156 B／共有120 Bで、元Managerに対して12 B増。
別objectのDomainは40 B、Pinsは1 B、buffer3本は204 B、共通観測変数のsymbol合計は103 B。
合計RAMにはlink時の整列があり、objectの増分53 Bとimage全体の差52 Bは一致しない。
LTOなしでは接続による全RAM差が56 Bとなる。
Flash差には、追加したDomain変更・publish・Pins宣言・台帳観測の操作経路も含む。wrapper単独の命令数ではない。

全28構成の.dataは0 B。未使用8構成はManaged／Domain／Pins／buffer・RAMが0 Bで、空の8 B imageとhashも一致した。
allocatorとconstructor arrayは検出していない。
出力された最大単体frameはV003で32 B、X035で80 B。呼出しの積算stack量やIRQを含むpeakではない。
entryは独立linkの構造観測用であり、startup・upload・実行は行っていない。

## 未検証と次の接続

Domainは協調的・非取消の直列台帳で、他の呼出し元がleaseを強制破棄する使い方は契約外。
ResourceFaultは内部のrelease不成立を返す防御的結果であり、強制破棄後の復旧や安全な周辺停止を証明しない。
Core・Domain・bufferは借用終了まで生存させ、破棄による自動停止は提供しない。

今回のPinsはreadyの宣言boolで、[X035準備／route adapter](x035-i2c-route-result.ja.md)とは未接続。
Domainも実clockを変更・測定せず、論理maskはpackageのpadやUSBとの物理競合を表さない。
IRQ禁止・DMAなし・register独占は既存Masterの前提を引き継ぎ、このwrapper自体では実装しない。
実siliconの停止／drain・clock喪失・周辺reset、ISRとの共有、event配達、V006休止への接続を確認する必要がある。
自動close、予約要求の閉鎖、待機・公平性や公開APIの採否はここでは決めない。

証拠は `work/exp01-i2c-managed-handoff-20261021-evidence.tar.gz`、1,796,501 B、
SHA-256 `d3d49c0ed158ac8eb5fdbbf63c7f73b830f3b160625cc645924a732730b4bc1b`。
native実行物2本・108 trace・28 ELF・全入力・tool記録・JUnit・再集計scriptと比較対応表を含む。
[変更課題](i2c-managed-handoff-exercise.ja.md)は利用者レビュー前である。

後続の[完了callbackとの接続](../EXP-03/i2c-event-result.ja.md)では、このManagedと既存配信glueを変更せず、周辺返却後の結果配達・別owner通信中のcallback回収を検査した。発生元が異なる同値request tokenの誤配信の反例も保存した。実Pins／IRQへの接続は未検証のまま。
