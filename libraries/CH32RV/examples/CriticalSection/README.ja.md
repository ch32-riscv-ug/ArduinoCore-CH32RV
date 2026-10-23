# CriticalSection

[English](README.md)

割り込みと共有する値を、短い割り込み禁止区間でコピーする書き方を示します。

## 必要なものと配線

外部入力は不要です。UART モニタを接続します。

## 操作と期待する結果

`counter`、`seconds`、`ticks` が表示されます。この例の変数更新は `loop()` 内で行っており、実際の ISR との競合を発生させる試験ではありません。

## API と処理の流れ

`noInterrupts()` と `interrupts()` の間で、整合させたい値だけをコピーします。表示はその区間の外で行います。`volatile` は ISR から変わる値の読み書きを省略させないための指定です。

## 改造するときの注意

この対は入れ子の管理をしません。汎用関数から使う場合は元の割り込み状態を戻せる [SyncKit の CriticalSection](../../../CH32RVSyncKit/examples/CriticalSection/README.ja.md) も参照します。禁止中に `delay()` や待ちを伴う送信を入れません。

## 関連資料

- [スケッチ](CriticalSection.ino)
- [基本サンプルの準備と一覧](../README.ja.md)
- [スケッチの組み立て方](../../../../docs/sketch-guide.ja.md)
