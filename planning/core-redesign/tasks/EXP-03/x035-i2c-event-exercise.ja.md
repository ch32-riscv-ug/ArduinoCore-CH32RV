# X035の再準備と未回収結果を予測する

2026-10-21。[X035準備adapterとcallbackの接続](x035-i2c-event-result.ja.md)に対応する教材案。
利用者レビュー前であり、公開APIやloop／delayの自動進行方針を確定するものではない。

## 予測する

AがI2Cをopenして要求を完了し、reapした。Aの結果と受信bufferは未回収である。
次に、周辺をBへ渡し、48 MHz／400 kHzから12 MHz／100 kHzへ変更したい。
次の操作がいつ許可されるかと、Aのbufferをいつ読めるかを予測する。

- Aがまだ周辺をopenしている状態で、pin準備を再実行する。
- Aの周辺だけをcloseし、結果を残す。
- Domainへ新PCLKをpublishするが、観測するRCCの分周設定をまだ変更していない。
- RCCの模型を新しい分周値へ変え、pin準備を再実行する。
- Bをopenして通信中に、Aの登録済みcallbackへ配達する。

準備済みフラグがtrueでも、open時のclock・gate・route・pin再確認に失敗すれば周辺はopenできない。
またreadyは「今、新たにopenするための条件」の述語であり、PEが有効な通信中にreadyがfalseになることだけを故障と解釈しない。

## 再現と変更

repository rootで、ボードへ接続せずに実行する。

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_x035_i2c_event.py -q
```

native実行物はC8／F7・route 0〜7の16本。各実行物で専用／共有record方式を選ぶ。
`EXP03_X035_I2C_EVENT_EVIDENCE` に未作成directoryを指定すると、全入力・実行物・traceを保存できる。
初回のcompileには時間がかかるが、実機操作やuploadは行わない。

`test_reprepare_with_old_result_retained_and_callback_during_new_owner` がA／Bの再準備trace。
`prepare 1` はcold/quiescentを呼出し元が宣言して準備を試す操作。
`change`／`publish Hz` は台帳の宣言、`set32` はregister模型への刺激であり、実clockを変更する機能ではない。
`abandon 1` も外部の停止根拠を宣言するだけで、resetや周辺停止を代行しない。

予測してから、次を個別に変更して状態とMMIOを記録する。

1. Aのcloseをreap前／reap後へ移す。準備を妨げるのが通信の未回収か、利用者結果の未回収かを区別する。
2. 新PCLKのpublishだけを行い、RCC刺激を省く。ClockMismatchとPinsNotReadyのどちらの操作で拒否されるか。
3. prepare成功後にrouteまたはpin設定を変え、再prepareせずbackendをopenする。失敗したopenのleaseが残らないか。
4. C8 route 0をF7 route 0へ変更する。対象packageに存在しないpadが、どの段階で拒否されるか。
5. debug routeで既存のdebug解放宣言を省く。勝手にdebugを無効化せず、FaultHeldとして資源を保持することを確認する。
6. gate／route／pinの書込みを一つ無視させる。再試行前のabandonに、誰がどの停止根拠を用意すべきかを説明する。

## 評価記録

予測、実trace、変更差分、必要だった追加状態、誤解した操作名を記録する。
Domainの宣言値・RCCからの公称PCLK・実clockの測定値を混同しない。
Fake上のabandon成功を、実機の安全な停止やclock喪失からの回復の証明にしない。
資源を返しても旧結果を保持できることと、そのbufferをBへ二重に貸せないことを説明できるかを確認する。
人による説明・変更の評価は、このhost検査の成功とは別に取得する。
