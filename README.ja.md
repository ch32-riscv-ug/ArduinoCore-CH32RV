# ArduinoCore-CH32

[English](README.md)

WCH CH32マイコン向けの、コミュニティ管理によるArduinoコアを新規設計するプロジェクトです。

> [!IMPORTANT]
> 現在は設計・調査段階です。インストール可能なArduinoコアや安定版リリースはまだありません。

本プロジェクトはWCHの公式プロジェクトではありません。プロジェクト名の`CH32`は対象デバイス系列を示すもので、公式性を示すものではありません。

## 目的

- 現代的なtoolchainとArduino環境で継続的にビルドできること
- 正確な型番・package単位で、対応範囲と検証状況を説明できること
- 外部SDKやEVTの変更をArduino APIから隔離すること
- 通常利用者がEVT APIへ降りずに使える、実用的なArduino APIとexampleを提供すること
- 小容量SKUの制約を明示しながら、可能な限りArduino標準APIへ準拠すること
- ビルド、書き込み、実機試験、Board Manager配布を自動化すること
- 初期実装の速さよりも、更新時の作業量と故障解析コストを小さくすること

## 現在の設計方針

現時点では、次の構成を有力案としています。まだ最終決定ではありません。

- 小さなArduinoコアを既定経路とし、EVT Compatibility Packは初期release要件にしない
- Arduino API境界には固定版の`ArduinoCore-API`を利用する
- startup、CRT、vector table、linker scriptは本プロジェクトが所有する
- WCH由来コードは必要なファイルだけをversion・hash・由来付きで取り込む
- デバイス、ボード、CI対象を宣言的manifestから生成する
- 書き込みフロントエンドと実際のprogrammer backendを分離する
- host test、compile test、HIL、logic analyzerを組み合わせる

初期実装の対象はCH32のRISC-V系列を想定しています。CH32FなどのArm系列、無線SoC、RTOSを含む最終的な対応範囲は未決定です。

## ドキュメント

次の作業を始める場合は、まず[引継ぎメモ](docs/handoff.ja.md)を参照してください。

- [ドキュメント一覧](docs/README.ja.md)
- [目的とスコープ](docs/project-scope.ja.md)
- [アーキテクチャ案](docs/architecture.ja.md)
- [旧コアの監査結果](docs/legacy-audit.ja.md)
- [外部エコシステム調査](docs/ecosystem.ja.md)
- [vendor取込方針](docs/vendor-policy.ja.md)
- [toolchain方針](docs/toolchain.ja.md)
- [書き込みとfixture](docs/upload-and-fixture.ja.md)
- [テスト戦略](docs/test-strategy.ja.md)
- [ロードマップ](docs/roadmap.ja.md)
- [未決定事項](docs/open-questions.ja.md)

## ライセンス

本プロジェクト自身のコードと文書は[MIT License](LICENSE)です。

将来取り込む可能性があるArduinoCore-API、WCH EVT、その他の第三者成果物には、それぞれ別のライセンスや利用条件が適用されます。ルートのMIT Licenseは第三者成果物を再ライセンスするものではありません。
