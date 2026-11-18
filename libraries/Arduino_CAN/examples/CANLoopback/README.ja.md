# CANLoopback

[English](README.md)

トランシーバもバスも無しで、CAN のコントローラを確かめます。サイレントのループバックで、1 秒ごとに標準 ID 0x123 の
4 バイトのフレームを送り、戻ったものをハードウェア `Serial`（115200 baud）に表示します。配線は要りません。
実際のバスで使うときは `setLoopback()` を外し、RX / TX にトランシーバをつなぎます。[Arduino_CAN](../../README.ja.md) も参照してください。
