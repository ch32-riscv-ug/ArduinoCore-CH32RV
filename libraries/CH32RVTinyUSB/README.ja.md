# CH32RVTinyUSB

[CH32RVUsbDevice](../CH32RVUsbDevice/) の土台になる [TinyUSB](https://github.com/hathach/tinyusb)
です。スケッチから直接インクルードするものではありません。

- `src/` は upstream のある commit の写しです。commit とファイルごとのハッシュを
  `vendor/tinyusb.lock.toml` に記録し、`tools/vendor/vendor_tinyusb.py --check` で照合します。
- パッチは1つです。`CFG_TUD_APP_DRIVERS_ONLY` で TinyUSB の組み込みクラス表を空にし、スケッチが
  宣言したクラスだけがリンクされるようにします。理由とともに `tools/vendor/vendor_tinyusb.py` に記録しています。
- upstream ではなくこのリポジトリのファイル: `src/tusb_config.h`、`src/ch32x035.h`。

CDC の数やバッファの大きさは、スケッチの `build_opt.h` で変えられます。`src/tusb_config.h` と
[docs/usb.ja.md](../../docs/usb.ja.md) を参照してください。
