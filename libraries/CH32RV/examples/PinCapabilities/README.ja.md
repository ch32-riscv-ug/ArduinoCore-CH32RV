# PinCapabilities

[English](README.md)

variant が定義する端子の有無と ADC の対応を調べます。

## 必要なものと配線

UART モニタを接続します。ほかの配線は不要です。

## 操作と期待する結果

起動時に `A1`、バス端子等の情報と有効な pad の一覧を表示します。再表示したい場合はリセットします。

## API と処理の流れ

`digitalPinIsValid()` は端子の有効性、`digitalPinHasADC()` は ADC の対応を問い合わせます。数値を連番と仮定せず、`PA1` 等の名前や variant の別名を使います。

## 改造するときの注意

表示はビルドに使った variant の情報です。基板のコネクタへ引き出されているか、別の部品につながっているかは回路図で確認します。端子を電気的に走査して検出するスケッチではありません。

## 関連資料

- [スケッチ](PinCapabilities.ino)
- [基本サンプルの準備と一覧](../README.ja.md)
- [スケッチの組み立て方](../../../../docs/sketch-guide.ja.md)
