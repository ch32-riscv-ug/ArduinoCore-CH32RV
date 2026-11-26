// en: Scan a hardware I2C bus every 5 seconds and print the addresses that acknowledge.
// ja: ハードウェアI2Cを5秒ごとに走査し、応答するアドレスを表示する。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

#include <Wire.h>

// en: The bus pins. SDA / SCL are this chip's default I2C pads. For other pads, name them here.
// ja: バスの端子。SDA / SCLはこの石の既定のI2Cの端子。ほかの端子を使うときはここで指定する。
static const int SDA_PIN = SDA;
static const int SCL_PIN = SCL;
// en: X035's PC18 / PC19 are the debug pads (see USE_DEBUG_PADS below), e.g. on the WeAct CH32X035:
// ja: X035のPC18 / PC19はデバッグの端子（下のUSE_DEBUG_PADSを参照）。WeAct CH32X035なら:
// static const int SDA_PIN = PC18;
// static const int SCL_PIN = PC19;

// en: 1 when the pins above are the debug interface's pads: the debugger lets go of them until the next
// en: reset, and no WCH-Link or OEP probe can attach meanwhile (upload over USB instead).
// ja: 上の端子がデバッグの端子のときは1にする。次のリセットまでデバッガーが端子を手放し、その間は
// ja: WCH-LinkやOEPのprobeから接続できない（書き込みはUSBで）。
#define USE_DEBUG_PADS 0

// en: Where the results go: Serial (a UART), or SerialUSB on a board you upload over USB
// en: (then also #include <CH32RVUsbDevice.h> and call SerialUSB.begin() instead of Serial.begin()).
// ja: 結果の出力先。Serial（UART）か、USBで書き込む基板ならSerialUSB
// ja: （そのときは #include <CH32RVUsbDevice.h> を足し、Serial.begin() の代わりに SerialUSB.begin() を呼ぶ）。
#define Console Serial

#if USE_DEBUG_PADS
#include <CH32RV.h>
#endif

static bool busReady;

void setup()
{
    Console.begin(115200);
#if USE_DEBUG_PADS
    CH32RV.releaseDebugPins();
#endif
    // en: Choose the physical bus pins before probing addresses on that bus.
    // ja: バスの実端子を指定してから、そのバス上のアドレスを調べる。
    busReady = Wire.begin(SDA_PIN, SCL_PIN);
}

static void scan()
{
    Console.print("scanning I2C");
    if (busReady) {
        // en: The I2C peripheral (I2C1, I2C2, ...) these pins belong to.
        // ja: この端子がつながるI2Cの周辺機能（I2C1、I2C2など）。
        Console.print(Wire.peripheral());
    }
    // en: digitalPinToName() gives the pad's name (PC19), not the number the core keeps for it.
    // ja: digitalPinToName()は端子の名前（PC19）を返す。コアの内部の番号ではない。
    Console.print(" on SDA=");
    Console.print(digitalPinToName(SDA_PIN));
    Console.print(" SCL=");
    Console.print(digitalPinToName(SCL_PIN));
    Console.println();

    if (!busReady) {
        // en: The pins are no I2C route of this chip, or a debug pad not yet released.
        // ja: その端子の組はこの石のI2Cの経路に無いか、まだ手放していないデバッグの端子。
        Console.println("  these pins cannot carry I2C here (a debug pad? set USE_DEBUG_PADS)");
        return;
    }

    unsigned found = 0;
    for (uint8_t address = 1; address < 127; address++) {
        Wire.beginTransmission(address);
        // en: 0 is an ACK; 2 is no answer at that address. An ACK identifies a responding address,
        // en: not the device type.
        // ja: 0はACK、2はそのアドレスに応答なし。ACKで分かるのは応答するアドレスで、機器の種類ではない。
        const uint8_t result = Wire.endTransmission();
        if (result == 0) {
            Console.print("  device at 0x");
            if (address < 0x10) {
                Console.print('0');
            }
            Console.println(address, HEX);
            found++;
        } else if (result == 5) {
            // en: 5 is a timeout: SCL or SDA never went high. No pull-ups, a device holding the line
            // en: low, or pins that are not actually I2C (a debug pad still the debugger's).
            // ja: 5はタイムアウト。SCLかSDAがHighに戻らない。プルアップが無い、機器が線をLowに
            // ja: 押さえている、端子が実際にはI2Cになっていない（手放していないデバッグの端子）のどれか。
            Console.println("  bus timed out - pull-ups fitted? a line held low?");
            return;
        }
    }
    Console.print("done, ");
    Console.print(found);
    Console.println(" device(s)");
}

void loop()
{
    scan();
    delay(5000);
}
