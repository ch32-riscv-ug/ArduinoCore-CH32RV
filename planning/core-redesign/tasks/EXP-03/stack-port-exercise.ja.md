# stack数と静的確保を数える

[固定portの配置](stack-port-result.ja.md)を用いる教材案。人による評価前で、製品APIではない。

1. アプリ2 task、各512 Bのstackで必要なstack配列量を計算する。idleも含める。
2. TCB・queue・scheduler状態と起動／IRQ領域を足し、表のRAM総予約と照合する。
3. portがIRQ上端から512 B下げてspを設定する場合、IRQ予約512 Bだけでは足りない理由を図にする。
4. NOLOAD予約だけを変えてbinary hashが一致する例から、binary sizeだけではRAMを計算できないことを説明する。
5. cooperative設定のFreeRTOSと、2本だけのstackを交互に使う方式の違いを説明する。
6. `.su`最大値・静的stack配列量・観測peakを区別し、今回測れていない項目を列挙する。
7. RV32EのAssembler拒否が、この固定portについて示すことと、FreeRTOS全般について示さないことを分ける。

この課題でkernelの待機や切替が動いたことにはしない。実機runと人の理解度評価は別に記録する。
