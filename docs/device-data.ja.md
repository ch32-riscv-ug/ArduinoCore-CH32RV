# Device data の利用

## 正本と固定

デバイス、型番、package、memory、pin、route、割り込み等の正本は
[`ch32-riscv-ug/ch32-device-data`](https://github.com/ch32-riscv-ug/ch32-device-data) です。
このリポジトリは consumer であり、通常ビルド時に上流へアクセスしません。

採用 revision は `vendor/ch32-device-data.lock.toml` に次の値で固定します。

- Git commit
- consumer surface の schema version
- `index/manifest.csv` の SHA-256

manifest は consumer が読む全ファイルの hash を含むため、固定 commit と合わせて入力を一意にします。

## このリポジトリに置くもの

- `boards.txt`
- `variants/<SERIES>/pins_arduino.h`
- 型番ごとの linker script
- vector、IRQ、EXTI、clock 初期化に必要な生成 header
- 生成元を特定する lock file

これらは release 時にネットワークなしでビルドできるよう commit します。生成物は手編集せず、
生成器で再作成します。

## 更新

```sh
uv run --no-project python tools/generate/generate.py \
  --tables /path/to/ch32-device-data --platform .
uv run --no-project python tools/generate/generate.py \
  --tables /path/to/ch32-device-data --platform . --check
```

正確な取得、差分確認、lock 更新の手順は
[`tools/generate/README.ja.md`](../tools/generate/README.ja.md) を参照してください。

更新時は、入力 revision、lock、生成物を同じ変更として review します。データ不足をこのリポジトリの
手書き例外で恒久化せず、再利用可能な事実は `ch32-device-data` 側で修正します。

## リポジトリを分ける理由

device data は Arduino core だけでなく uploader、probe、viewer、文書生成からも利用されます。
独立した正本にすることで、Arduino 固有形式へ閉じ込めず、全 consumer が同じ provenance と schema を参照できます。
