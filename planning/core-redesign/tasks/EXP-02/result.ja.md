# EXP-02 直列データ部品の比較結果

2026-10-20。通常モデルで進められる最初の比較単位を完了した。
対象は実行器・heap・ブロック待機を使わない直列文脈の部品。
ISR・複数producer・コア間の保護、非自明な型、待機接続を含むEXP-02全体は未完了。

## 部品と意味

| 部品 | 確認した契約 |
|---|---|
| 固定／借用queue | 容量1／4／16、受理・拒否、FIFO、wrap、空→非空のready flag。trivial payloadだけを許可 |
| byte ring | 容量16／64／256、部分受理、wrap、reserve／commit、peek／consume。未確定データを公開せず、借用中の範囲を上書きしない |
| message buffer | 2 byte長＋payload。最大8／32 byte、領域32／128 byte。全体受理、出力不足では未消費、長さ0も扱う |
| semaphore | binary／counting、上限、空、初期値不正、overflow拒否 |
| flag／lock | any／all、観察と消費、mask0、ownerと非再帰、誤った解放を拒否 |
| pool | 全貸出、満杯、返却、二重返却・古い世代を拒否、block整列、世代上限で退役 |

WakeFlagは短いflag設定だけで、ユーザーcallback・timer・実行器へ依存しない。
空確認・通知消費・登録の競合を解決する接続は次の単位。
poolの世代保護には管理領域と退役の制約があり、無保護index poolより常に有利という意味にはしない。

## Host検査

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_data_parts.py -o junit_family=xunit1 \
  --junitxml=work/exp02-data-host-audited-20261020/junit.xml
```

最終入力コピーを保存して92件成功、15.98秒。AddressSanitizer／UndefinedBehaviorSanitizerを使用。
queue40条件、ring30条件、message20条件で各10,000生成ステップを独立deque／byte streamへ照合した。
残る2件は借用境界と同期・pool・型の直接確認。uint8／uint32／16 byte型のcopy、
最小世代幅の255回貸出後の退役、非整列サイズのblockでも各先頭が4 byte整列することを含む。
pytest pluginのexperimental warningが92件あり、失敗はない。

## Link比較

```sh
python3 tests/platform/core_redesign/measure_data_parts.py \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --out work/exp02-data-matrix-20261020-audited
```

50ケース×2 ABI×LTO有無の200構成がlink成功。
queueは3容量×3 payload幅×固定／借用／通知の27ケース。
16 byte copyは全wordを観測し、flagのset／clear／takeは別の刺激で進める。
入口で全状態が消去されるだけの不適切な条件は、最終表に使っていない。

9種類の未使用object×4条件の36行で、Flash・静的RAMの追加0 byte、init_arrayなし、component symbolなしを確認。
初回の非constexpr constructorではqueue未使用でも全4条件で追加Flash36／RAM20 byte、init_array4 byteが残った。
constructorを定数初期化可能にし、修正前の入力と結果を残して再評価した。
これはリンク到達性の観測で、MCUでconstructorを実行した証明ではない。

RV32E・LTOなしの例を示す。単位はbyte。

| ケース | Flash image | 静的RAM | baselineからのFlash増分 | RAM増分 |
|---|---:|---:|---:|---:|
| baseline | 28 | 12 | 0 | 0 |
| uint32×4固定queue | 120 | 32 | 92 | 20 |
| uint32×4借用queue | 310 | 40 | 282 | 28 |
| uint32×4通知付きqueue | 188 | 40 | 160 | 28 |
| ring64 | 646 | 84 | 618 | 72 |
| message領域32／最大8 | 726 | 52 | 698 | 40 |
| semaphore上限4 | 86 | 16 | 58 | 4 |
| event bits | 170 | 16 | 142 | 4 |
| owner付きlock | 68 | 16 | 40 | 4 |
| 16 byte block×4のpool | 212 | 96 | 184 | 84 |

これらはvolatileな刺激と観測、必要なlibc／libgccを含むlink-onlyプログラムの値。
class単独の最小費用、完成コアの費用、実行時間やstack最大量を意味しない。
借用storageと固定容量では管理情報・定数展開の条件が異なるので、便利さと増分を分けてP5へ渡す。
全条件は [size-matrix.csv](size-matrix.csv)、tool・入力hashと200行の記録は [measurements.json](measurements.json)。

## 証拠と次の作業

入力HEADは `4051d96`。hostとmatrixの実行前入力コピーを保存した。
compiler・内部helper・binutils・ABI別libraryのhashとtool lockを記録し、各rowにELF hashを付けた。
生の入力・コマンド・ELF・map・逆アセンブル・symbol・section・hash一覧は
`work/exp02-data-matrix-20261020-audited/`、hostは `work/exp02-data-host-audited-20261020/`。
各baselineとの差分と36未使用行のsymbol・初期化表を別途照合した。

移送用は `work/exp02-serial-20261020-evidence.tar.gz`、1,510,729 byte、
SHA-256 `e60478ebd73f23fafedf67cef0834672001ec8efaed15348742802bf07eb8e90`。
修正前のconstructor保持条件も含む。新しいcheckoutと固定toolで上記コマンドを再実行できる。
未コミットの入力を渡す場合、archiveのhost／matrixのinputsを相対pathへ戻す。

実機のcycle・IRQ禁止時間・stack peakは未計測。ISRや複数コアからこの直列実装をそのまま呼べるとはしない。
次は [非自明な型・保護・通知登録の比較](next.ja.md)と、EXP-03の実行モデル比較を進める。
通常の修正で済んでおり、高位モデルの採用判断はまだ行っていない。
