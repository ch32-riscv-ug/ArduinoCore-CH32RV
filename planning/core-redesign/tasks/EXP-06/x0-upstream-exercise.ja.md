# 上流の直接実装を分割する前に比較条件を読む

2026-10-21。[X0参照元の再現・観測](x0-upstream-result.ja.md)に対応する教材案。
特定APIやcontroller protocolの採用は決めず、人による評価も未実施。

上流の小さいimageと新しい共通部品のimageを比べる前に、機能・失敗処理・buffer・stackが何を含むかを説明する課題である。

## 予測と観測

結果文書のコマンドで、固定sourceを取得・buildし、native recorderを生成する。
実機・PD sourceへ接続せずに次を確認する。

- OLEDの0x78とINAの0x80を、7 bit addressを要求するMasterへそのまま渡すと何が違うか。
- clearLine・printVal・NO PPS表示は何byteを一つの取引で送るか。payload32 Bとheaderのstorageをどう数えるか。
- INAの用途値mv／maの計8 Bと、wire上のregister指示・2 B read×2の関係は何か。
- prepared済みのhardware I2Cを上流のPA5／PA6へ接続したことにできるか。どの配線条件を確認すべきか。
- 上流の128×32 vertical初期化へ、128×64 page順のbyte列を流すと、何を根拠に正しい画像と判断できるか。

native recorderの例。outは再現toolで実際に指定した場所に置き換える。

```sh
work/x0-upstream-new/native/recorder clear-line 0 0
work/x0-upstream-new/native/recorder value 12345 0
work/x0-upstream-new/native/recorder ina-read 2 4660
```

記録は関数がI2C APIへ渡した値であり、実busの波形・ACK・表示器内部の状態ではない。

## 分割案を記述する

コードを変更する前に、次を図または表へ書き出す。

1. 使用中buffer、未送信部分、表示revision、部分失敗、完了回収を誰が保持するか。
2. 一度に32 Bを送った後、何を保持して呼出し元へ戻り、どの明示操作で次へ進むか。
3. INAの電圧だけ成功し電流で失敗した場合、前値・stale・新しい値をどう区別するか。
4. PD_negotiate内の待機をそのままcallbackへ入れた場合、同じ文脈の他処理へいつ戻れるか。
5. 入力・表示・失敗処理が違うUとX0を、どの差分を揃えてから費用比較するか。

rendererを小さくしたり失敗処理を省いたりして候補の数値を有利にしない。
controller仕様と比較契約に未確定部分があれば、その部分を未確定として残す。
追加状態・source境界・失敗時の動作を説明できるかを人が確認し、hostの成功だけで教材評価を完了にしない。
