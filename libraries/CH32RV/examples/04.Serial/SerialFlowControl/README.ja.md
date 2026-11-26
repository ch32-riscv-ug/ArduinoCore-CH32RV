# SerialFlowControl

[English](README.md)

`Serial` のハードウェアのフロー制御です。スケッチがゆっくり読む間 RTS で速い送り手を待たせ、CTS で相手の準備ができるまで
自分の送信を止めます。

## 必要なものと配線

RTS / CTS のある USB-UART アダプタ（FT232R、CP2102N など）。ボード TX→アダプタ RX、ボード RX←アダプタ TX、ボード RTS→
アダプタ CTS、ボード CTS←アダプタ RTS、GND 共通。ボードの CTS / RTS は Serial の route にある USART 自身の端子です。
variant の `pins_arduino.h` の `CH32RV_SERIAL1_CTS_ROUTES` / `_RTS_ROUTES` を見てください（CH32V003 の既定の route
なら CTS は PD3、RTS は PC2）。端末側でもハードウェアのフロー制御を有効にします。

## 操作と期待する結果

115200 8N1、RTS/CTS で開きます。ボードが `Flow control on` を出します。長い文を貼り付けると、1 バイト 5 ms でゆっくり、
欠けずに全部戻ります。読まれていないバイトが 32 になるたびにアダプタが待つからです。端末のフロー制御を切ると、
63 バイトのバッファがあふれた所から文字が欠けます。

`This route has no CTS / RTS pads` と出たら、Serial の route の USART に CTS / RTS が無く（UART4〜8 の一部など）、
`setHwFlowCtrlMode()` が false を返しています。

## API と処理の流れ

`setHwFlowCtrlMode(UART_HW_FLOWCTRL_CTS_RTS, 32)` は `begin()` の前でも後でもかまいません。読まれていないバイトが
32 になると、USART は次の 1 バイトをデータレジスタに残し、そのストップビットで RTS を上げます。`read()` で空きが
できるとそのバイトが入り、RTS が下がります。CTS はプルアップするので、何もつないでいなければ何も送りません。
`setPins(-1, -1, cts, rts)` でほかの端子を名指せます（その route の USART の CTS / RTS であることは同じ）。

## 改造するとき

`UART_HW_FLOWCTRL_RTS` か `_CTS` だけにすると、その端子だけを使います。しきい値を小さくすると、送り手が止まるまでの
余裕が増えます。最大はバッファ（既定 63 バイト、`CH32RV_SERIAL_RX_BUFFER_SIZE` - 1）です。

## 関連する文書

- [スケッチ](SerialFlowControl.ino)
- [例の準備と一覧](../../README.ja.md)
