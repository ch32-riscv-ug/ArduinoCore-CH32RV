# EXP-02 次の比較単位

通常モデルで続ける。現在の直列文脈・trivial payloadの部品と容量測定を出発点とする。
P4までの全体範囲は[実行状況](../../execution-status.ja.md)を維持し、個別部品の成功で他の契約を埋めない。

1. 非自明な型について、placement storage、moveとdestroy、満杯・回収・破棄の寿命を比較する。
   任意のconstructor／destructorをISRへ許すという意味にはしない。
2. 単一コアISR共有の保護policyを、通常処理専用と分ける。
   保存した割り込み状態の復元、入れ子、保護区間中に到着した刺激をhostで検査し、
   actual targetのIRQ mask方法と順序は一次資料・実backendで別に確認する。
3. 複数producerとコア間のmemory orderingは、単一コアの割り込み禁止と混ぜない。
   hostで使用するatomic／lockの意味と、MCUが要求する操作・配置を分けて記録する。
4. WakeFlagの消費とempty確認・再登録の境界へ到着を注入し、失われる起動がない接続を作る。
   busy loopやユーザーcallbackへのinline配達を暗黙に加えない。
5. try部品を実行器へ接続する前に、未使用・即時操作・通知追加の費用を再確認する。
   待機wrapperはEXP-03のproducer進行条件と同じ契約で比較する。

通常の不具合は修正を続ける。capacity、所有、寿命、公開実行文脈の比較契約を変更する必要があるときだけ高位判断へ戻す。
実機を使えない項目は未検証として残し、EXP-03のhost／compileで独立して進められるものを継続する。
