# CH32X035 / generic probe I2C peer

CH32X035のI2C1 route 2（PC16=SCL、PC17=SDA）から、hostがmanifestで割り当てた
probe I2C targetへ一回の4-byte writeを送るHIL sketchである。X035はI2Cを一系統しか
持たないため、二系統loopbackの代替ではない。

このsketchはP4の現行direct-IDF targetが持つ一回だけのreceive jobに対応する。合格には
targetの`peer_address_ack PASS`、`peer_write_accepted PASS`と、probe statusの
`rx_transactions=1`を同じrun artifactへ残す。使用中のESP-IDF callback ABIは受信長を返さないため、
`last_rx_length`を内容検証の根拠にしない。probeのI2C targetを
先にatomic leaseで開始し、終了時は必ずreleaseする。

PC16/PC17はX035ではUSB D-/D+と共有する。USBを同時使用せず、3.3 Vの外部pull-upが存在する
ことを確認してから実行する。read/repeated START/継続transfer/100 kHzはまだ合格範囲外である。
