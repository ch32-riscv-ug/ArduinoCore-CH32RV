# ShiftOut

[English](README.md)

シフトレジスタへ1 byteずつ送り、出力を順番に切り替えます。

## 必要なものと配線

74HC595 の DS→`DATA_PIN`、SHCP→`CLOCK_PIN`、STCP→`LATCH_PIN`、OE→GND、MR→電源を接続します。電源・GND と出力 LED の抵抗も用意します。

## 操作と期待する結果

Q0～Q7 に接続した LED の点灯が順に移ります。ビット順と LED の接続順を照合します。

## API と処理の流れ

`shiftOut(data, clock, MSBFIRST, value)` はソフトウェアでクロックとデータを出します。送信中は latch を Low にし、送信後に High にして出力へ反映します。

## 改造するときの注意

三つの端子は SPI 専用端子である必要はありません。接続先 IC の電源に対する入力 High の条件と、各出力・IC 全体の電流条件を確認します。

### シフトレジスタの接続

16 pinの74HC595配置では、DSは14、SHCPは11、STCPは12、OEは13、MRは10です。OEをLow、MRをHighに保ちます。使用するパッケージの配置を部品資料で確認し、LEDには電流制限抵抗を付けます。電源電圧と入力Highの条件もMCUのI/Oに合わせます。

## 関連資料

- [スケッチ](ShiftOut.ino)
- [基本サンプルの準備と一覧](../README.ja.md)
- [スケッチの組み立て方](../../../../docs/sketch-guide.ja.md)
