# 実験0008: LTO×weak ISRとinterrupt attributeの静的検証

実施日: 2026-08-19
対象question: Q-024(LTO)、Q-021(割込みABIの静的部分)、Q-025(WCH fork互換)
実施環境: WSL2 Linux x86_64、xPack riscv-none-elf-gcc 14.3.0-1。逆アセンブル検証のみ(実行なし)

## 目的

旧コアで問題源だった「weak ISRとLTOの相互作用」と、upstream GCCのinterrupt attributeの挙動を、実装前に静的検証する。

## 結果

1. **LTO×weak ISR×vector table: 問題なし**。グローバルコンストラクタ+`SysTick_Handler`上書きを含むC++アプリを`-flto`(compile側は`-ffat-lto-objects`付き、crt0はアセンブリのため非LTO)でlinkし、以下を確認:
   - vector tableのSysTickエントリが上書きハンドラを指す(Default_Handlerへ退化しない)
   - table全体(46 word)がKEEPで保持され、`.init_array`も残存
2. **`__attribute__((interrupt))`(upstream)はrv32eで正しいISRを生成**: 使用レジスタ(a4/a5)のみをstackへ退避し`mret`で復帰するprologue/epilogueを確認。machine mode既定
3. **`__attribute__((interrupt("WCH-Interrupt-fast")))`はupstreamでは警告のみで無視される**: `-Wattributes`警告の後、**通常関数として**コンパイルされる(mretなし)。エラーにならないため、WCH fork向けコードをupstreamでビルドすると**静かに壊れる**。コア/exampleでこの属性を使う場合はプリプロセッサで明示的にガードし、CIで検出する必要がある

## 結論

- LTOには少なくとも「weak ISR/vector/constructor」由来の既知障害はない(実測1構成)。default化の判断(Q-024)にはsize/debug/再現性の比較が残る
- Arduino APIの`attachInterrupt`実装はupstreamの`interrupt` attributeで成立する。WCH独自の高速割込み(HPE)対応はfork lane限定の最適化として扱う(Q-021の実測=latency比較はHIL待ち)
- 「WCH-Interrupt-fast属性の静的検出」をvendor由来コード取込時のcheck項目に加える

## 再現手順(要点)

実験0002のharness構成でapp.cppに`-flto`を付けてlink → `.vector`のword列をnmと突合。interrupt attributeは単体.cのobjdumpで確認。
