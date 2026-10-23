// en: Print startup text and uptime through the SDI debug channel.
// ja: SDIのデバッグ経路へ起動メッセージと経過時間を出す。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

#include <SerialSDI.h>

void setup()
{
    // en: The baud argument is compatibility-only; no UART clock is configured.
    // ja: baud引数は互換用で、UART clockは設定しない。
    SerialSDI.begin(115200);
    // en: This uses the debug output object, so the host must collect SDI rather than open the MCU UART.
    // ja: デバッグ出力のオブジェクトへ書く。ホストはMCUのUARTではなくSDIを読み出す。
    SerialSDI.println("hello from the debug module");
}

void loop()
{
    SerialSDI.print("uptime ");
    SerialSDI.print(millis() / 1000);
    SerialSDI.println(" s");
    delay(1000);
}
