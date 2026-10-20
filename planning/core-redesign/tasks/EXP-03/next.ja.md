# EXP-03 継続単位

通常モデルで続ける。[有限workの準備](work-preparation.ja.md)はRUN-01／02の一部で、
EXP-01との結合やEXP-03全体を完了にしない。[全作業の状態](../../execution-status.ja.md)も維持する。

2026-10-20追記：項目1の独立host契約は [I/O所有model](../EXP-01/io-preparation.ja.md)で検査した。
物理backend・配達済み通知とendの接続・診断なし費用は残る。次はtimerと宛先mailboxへの結合。
項目2の期限と通知保持の独立部分は[期限model](timer-preparation.ja.md)で検査した。
時計管理・sleep・複数timer管理と実行器への結合は残る。
項目3の通常直列文脈のmailboxとI/O／timer接続は[宛先結合](event-preparation.ja.md)で検査した。
次は項目4の同等機能と待機条件、項目5の独立対照、および実共有・完成用途を進める。

1. EXP-01のhost I/O契約を先に固定・検査する。受理／busy／不正／未対応、終端結果のsticky保持、
   cancel受理とhardware停止の区別、成功・失敗・cancelの両順序、end後の古い通知抑止、buffer回収。
   fake backendと物理backendの証拠を分け、actual peripheralの動作を推測で補わない。
2. TIME-01／02の時計と判定を独立化する。半周未満、周回、期限直前／一致／直後、停止／再設定、
   遅れた周期の最新1回への集約。tickと次期限方式、sleepによるclock差は別条件。
3. Cの宛先mailboxでpayload FIFOと集約通知を分け、1対象1回・有限snapshotへ接続する。
   overflowでも必要なI/O終端を失わない。解除・再利用・fan-outの途中拒否を含む。
4. 共通I/O・データと同じ入力をA/B/Cへ接続する。M/S/Uを同じ進行機会で検査する。
   try、非同期再開、loopからの同期wrapperを分け、通常処理／IRQ／別コアproducerの進行条件を表にする。
   callbackで禁止された待機は即時エラーとし、再帰配達やtimeoutで成功扱いにしない。
5. 独立stackのport・静的容量と深さ制限付き再入は基準と分けて調べる。
   targetの適合とstackを確認できない場合、hostの値で埋めない。

正しさが通った構成からABIの容量・初期化表・symbol・stack情報を取得する。
cycle、IRQ禁止時間、stack peak、実protocolは実機が必要な条件として残す。
EXP-04・05の独立host調査は並行順序で進められるが、agentの並列起動を要求する意味ではない。
公開文脈・所有・寿命を変える必要があるときだけ、高位判断へ戻す。
