# ADR-0008: Upload、discovery、monitor は `ch32rv` に一本化する

- Status: Accepted

## 決定

Board Manager には `ch32rv` を一つの platform tool として同梱し、次を担当させます。

- WCH-Link、OEP probe、対応 bootloader 経由の書き込み
- Arduino pluggable discovery
- UART、SDI、RTT、DMSEQ 等の monitor
- 対象 chip の識別と、指定対象との照合

`platform.txt` は exact part または series を `--chip` に渡します。tool database にない対象は
`target-not-in-db` で probe を開く前に失敗させ、自動検出で別 chip へ書き込む fallback は行いません。

## 理由

backend ごとの CLI と対応差を `platform.txt` に展開すると、書き込み、port 選択、monitor、安全確認の契約が
分散します。frontend を一本化すれば、IDE と CLI は同じ対象選択とエラーを使えます。

書き込み対象を fail-closed にするのは、未対応 series を選んだときに、接続中の別 chip へ誤って書き込むことを
防ぐためです。

採用 version と host 別資産は `tools/index/tools_ch32rv.json` を正本とします。
