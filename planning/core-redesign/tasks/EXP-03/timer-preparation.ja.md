# EXP-03準備：期限判定と保留通知の独立検査

2026-10-20。入力HEAD `baf5a9b`。
TIME-01／02の比較用に、時計・tick・実行器へ依存しない `DeadlineTimer` を作った。
物理clock、sleep中の時間、callback配達、実runtimeの方式を確定するものではない。

## この単位の意味

時刻幅は32 bit。相対期限と周期は半周未満とし、半周以上の指定を拒否する。
時刻の比較間隔にも半周の制約がある。32 bit値だけで長期停止の周回数を推測しない。
`arm(now, delay, period)` はone-shotならperiod=0。delay=0も受理するが、arm自体は配達しない。
時計が進むこと、`update(now)` で期限を判定すること、`take()` で通知を回収することを分ける。

| 操作／条件 | 実験の契約 |
|---|---|
| update | 明示呼び出しだけで判定。one-shotは一度だけ期限到達を保持 |
| 遅れた周期 | 当初の位相を維持し、到達した最新1回へ集約。nextをnow基準にずらさず、過去の全回数をcallbackで連続実行しない |
| 通知保留中の次期限 | 最新期限へ更新し、集約した回数を加算。readyを保持し、回収前に消さない |
| take | 最新の期限とskipped回数を一度だけ回収。空なら出力値を変更しない |
| stop | 未来の期限だけを停止。既に保留した通知は回収まで保持 |
| 再arm | 正常に受理した再設定は古い予定と未回収通知を明示的に置換。不正な設定は両方を保持 |
| skippedの上限 | UINT32_MAXで飽和し、超えた場合を別flagで示す。上限ちょうどとは区別 |

skippedは回収する最新1件以外の期限数。停止している通常処理へ時間保証を付けるものではない。
acceptedな再設定で古い通知を捨てることは、この試作の明示操作の意味として固定した。
既に外のmailboxへ入った通知の取消・世代管理は、このobjectの状態だけでは解決しない。
実行器との結合で別途検査し、公開APIのstop／cancel／replaceの意味はP5で判断する。

## Host検査

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_deadline_timer.py -o junit_family=xunit1
```

ASan／UBSan付き20件成功。既存plugin由来のexperimental warning20件。
Python側は周回しない整数時計と未回収の発生回数台帳を使い、C++のmodulo比較を写さずに照合した。

- 初回観測が期限の直前／一致／直後の場合、周回を跨ぐone-shot、遅い観測後の二重通知防止。
- 周期を飛ばした後の最新1回への集約、通知保留中の追加到達、次期限の元の位相。
- 半周−1の相対期限と周期、半周／UINT32_MAXの拒否、不正な再設定による通知の保持。
- stop後の未回収通知保持、正常な再設定による古い通知の明示置換。
- 1周期tickで複数周回に相当する60億tickを進め、各照合間隔は10億tickに制限した。
  skippedの飽和、UINT32_MAXちょうどと超過の区別、回収・再設定後のflag解除。
- 10 seed×20,000操作を独立台帳へ照合。通常の生成trace全体の進みは半周未満に制限した。

初回の飽和確認では、期待値を確認するテスト側の行番号がstopの行を指して失敗した。
台帳との全行照合は一致していたため、takeの行を検査するよう修正し、境界確認を追加して再実行した。
実装の飽和動作を書き換えて期待値へ合わせたものではない。

## ABIでの参考値

```sh
python3 tests/platform/core_redesign/measure_data_parts.py --matrix timer \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --out work/exp03-timer-link
```

5ケース×2 ABI×LTO有無、20構成がlink成功。
未使用4条件は追加Flash／静的RAM／init_arrayが0 byte、timer symbolなし。
RV32E・LTOなしの例。単位byte。

| 入口 | Flash image | 静的RAM | Flash増分 | RAM増分 |
|---|---:|---:|---:|---:|
| baseline | 40 | 20 | 0 | 0 |
| 実行時period | 482 | 44 | 442 | 24 |
| arm時にperiod=0 | 474 | 44 | 434 | 24 |
| arm時にperiod=10 | 474 | 44 | 434 | 24 |

volatileな時刻・操作・観測、skippedと期限と飽和flagを含むプログラムの値。
入口の違いによるcompilerの結果であり、tick対次期限のruntime比較ではない。
全行は [timer-size-matrix.csv](timer-size-matrix.csv)、入力・tool・ELF等は
[timer-measurements.json](timer-measurements.json)。

host `work/exp03-timer-host-20261020-final/`、link `work/exp03-timer-link-20261020/` に入力コピーと生記録を保存した。
移送archive `work/exp03-timer-20261020-evidence.tar.gz`、166,821 byte、
SHA-256 `8574c9900fce047d1231a1f0131a0e2a7b1a31b619dcdab39dfe0249467863a5`。
command・終了値・stdout／stderr・map・symbol・disasm・section・.su・hashを照合した。

## 残る接続と実機条件

次は有限workと宛先mailboxに接続し、時刻が進んでもhandlerが走るとは限らないこと、
配達前の解除・再設定、満杯時の保持、M/S/Uで許可する範囲を検査する。
複数timerの管理、tick／次期限、時計変更、sleep、実clockの分解能、ISR共有は残る。
この単位ではdelay(0)やloop外の自動処理を導入していない。
