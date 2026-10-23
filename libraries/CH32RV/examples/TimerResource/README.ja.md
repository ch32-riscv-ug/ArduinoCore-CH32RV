# TimerResource

[English](README.md)

タイマーを借り、基準周期と更新割り込みを設定するライブラリ実装者向けの例です。

## 必要なものと配線

UART モニタを用意します。GPIO の配線は不要です。

## 操作と期待する結果

`TIM lease: active` または `lost` と更新回数を表示します。取得・設定に失敗した場合も active にはなりません。

## API と処理の流れ

`ch32TimerTryAcquire()` で lease を得て、`ch32TimerApplyBase()`、`ch32TimerAttachUpdateInterrupt()`、`ch32TimerStart()` の順で使います。lease の有効性も確認します。

## 改造するときの注意

割り込みは回数を記録するだけです。`onQuiesce()` は同期的に呼ばれ、タイマー管理 API を再帰的に呼びません。普通の周期作業は [Timing](../Timing/README.ja.md) を使い、低水準の契約は [タイマー設計](../../../../docs/timer-design.ja.md) を参照します。

## 関連資料

- [スケッチ](TimerResource.ino)
- [基本サンプルの準備と一覧](../README.ja.md)
- [スケッチの組み立て方](../../../../docs/sketch-guide.ja.md)
