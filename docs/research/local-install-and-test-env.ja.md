# R-15: 開発中コアのインストール方式とテスト環境

調査基準日: 2026-08-19
文書状態: 方針記録(実測はこれから)
関連: [Q-015, Q-016](../open-questions.ja.md)、[テスト戦略](../test-strategy.ja.md)、[toolchain方針](../toolchain.ja.md)

## 目的

開発中のArduino platform(コア+boards.txt+tool参照)を、開発機とCIでどう実行・テストするかを決める。参考実装の`host-arduino-core`はtoolchainを持たないためsymlink方式で足りたが、本コアはtoolchain(xPack GCC等、R-04)を含むため、インストール経路そのものが検証対象になる。

## 2つの実行方式(方針)

| 方式 | 手順 | 検証できるもの | 向く場面 |
|---|---|---|---|
| **A: symlink直接実行** | `<directories.user>/hardware/<vendor>/<arch>`へ作業treeをsymlinkし、arduino-cliが直接読む | platform.txt/boards.txt/コアソースの変更を即時反映。ビルド・実行の内側 | toolchainが既にインストール済みの開発機での日常イテレーション |
| **B: ローカルHTTP+Board Manager経由** | package index JSONと(必要なら)platform archiveをローカルHTTPで配信し、`arduino-cli core install --additional-urls http://localhost:PORT/package_*.json`で入れる | **利用者と同じ経路**: index書式、tool参照(xPack直リンク)、checksum/size、展開、`{runtime.tools.*.path}`解決、新規data directoryへのclean install | release前検証、CI、tool定義やindex生成の変更時 |

使い分けの原則(ユーザー方針として合意済み):

- toolchainがない状態からの検証はB(実インストール経路)が好ましい
- toolchainが入っている状態での反復開発はA(symlink)がお手軽
- どちらを常用するかは**実際に動かして検証してから**決める

補足(一般知識、実測未了):

- 方式Aでも、tool(GCC等)は「別のインストール済みpackageのtool」を`{runtime.tools.<name>.path}`で解決できるため、一度Bまたは手動でtoolを入れれば以後Aで回せる見込み
- 方式Bのtool URLはlocalhostを指すindexの複製を生成すれば、xPackアーカイブのローカルキャッシュ配信も可能(GitHubへの重複DLを避ける)。checksumは同一なので差し替え自由

## 検証項目(今後の実測)

1. 方式Aで、tools参照(xPack GCC)が未インストールのとき何が起きるか(エラーメッセージの明瞭さ)
2. 方式Bをローカルで一周: index生成 → HTTP配信 → 新規`ARDUINO_DATA_DIR`でcore install → Blink compile。xPack直リンクtoolのDL・展開・パス解決を確認
3. 方式BでツールURLだけローカルキャッシュへ差し替えたindexの動作(CI高速化・rate limit回避)
4. `file://` URLがindex/tool参照で使えるか(HTTPサーバ省略の可否)
5. 各OS(Linux/Windows/macOS)での方式Bのclean install(toolchain.ja.mdのrelease前確認と同一手順の自動化)
6. CIでの標準構成の決定: 「方式Bで週次/release時のフル検証」+「方式Aでpush毎のcompile matrix」という2層案の妥当性

## 判断ポイント

- index生成(R-03のboards.txt生成と同じgenerator系)にローカル配信モードを最初から組み込むか
- CIのcompile matrixをA/Bどちらで走らせるか(速度 vs 忠実性)
- テスト用の暫定packager/architecture ID(Q-015)をこの検証で確定させるか
