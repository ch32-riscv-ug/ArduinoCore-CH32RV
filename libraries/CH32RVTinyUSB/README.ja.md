# CH32RVTinyUSB

[English](README.md)

[CH32RVUsbDevice](../CH32RVUsbDevice/README.ja.md) と [CH32RVUsbHost](../CH32RVUsbHost/README.ja.md) の土台になる [TinyUSB](https://github.com/hathach/tinyusb)
です。スケッチから直接インクルードするものではありません。

- `src/` は upstream のある commit の写しです。commit とファイルごとのハッシュを
  `vendor/tinyusb.lock.toml` に記録し、`tools/vendor/vendor_tinyusb.py --check` で照合します。
- `CFG_TUD_APP_DRIVERS_ONLY` / `CFG_TUH_APP_DRIVERS_ONLY` で Device / Host の組み込みクラス表を空にし、
  スケッチが使うクラスだけをリンクします。両ロールをビルド可能にしたまま未使用 stack をリンクしないための weak reference の変更もあります。
  全パッチと理由は [vendor_tinyusb.py](../../tools/vendor/vendor_tinyusb.py) と [lock](../../vendor/tinyusb.lock.toml) が正本です。
- このコアが追加する設定、系列別 header、DCD/HCD glue、IRQ 処理は lock の `ours` に記録します。
  `ch32rv_hcd_usbfs.c` はこのコアの Host controller driver です。既存 TinyUSB portable driver と区別します。

CDC の数やバッファの大きさは、スケッチの `build_opt.h` で変えられます。`src/tusb_config.h` と
[docs/usb.ja.md](../../docs/usb.ja.md) を参照してください。

## 変更と検証

```sh
python3 tools/vendor/vendor_tinyusb.py --check
```

リポジトリのルートから実行します。検証はオフラインで、固定した upstream と記録済みパッチに一致するか確認します。
これは実機での USB 転送の検証ではありません。upstream source を直接編集したまま hash を更新する運用にはしません。
取り込み方針は [ADR-0012](../../docs/adr/0012-usb-stack.ja.md) と [vendor policy](../../docs/vendor-policy.ja.md) を参照してください。

利用例は [Device](../CH32RVUsbDevice/examples/README.ja.md) / [Host](../CH32RVUsbHost/examples/README.ja.md) にあります。
USB PD は別の [CH32RVUsbPd](../CH32RVUsbPd/README.ja.md) が担当します。
