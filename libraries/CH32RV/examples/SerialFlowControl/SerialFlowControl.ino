// en: Pace a fast sender with RTS and let CTS hold what this board sends (hardware flow control).
// ja: RTSで速い送り手を待たせ、CTSで自分の送信を止める（ハードウェアのフロー制御）。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

// en: RTS goes high once this many bytes wait unread; the sender must stop until it falls again.
// ja: 読まれていないバイトがこの数になるとRTSが上がる。下がるまで相手は送るのを待つ。
static const uint8_t RTS_THRESHOLD = 32;

static bool flowOn;

void setup()
{
    // en: With no pins named, the USART's own CTS / RTS pads on Serial's route are used.
    // ja: 端子を名指さなければ、Serialのrouteにある USART自身のCTS / RTSの端子を使う。
    flowOn = Serial.setHwFlowCtrlMode(UART_HW_FLOWCTRL_CTS_RTS, RTS_THRESHOLD);
    Serial.begin(115200);
    Serial.println(flowOn ? "Flow control on: send a long text"
                          : "This route has no CTS / RTS pads: flow control is off");
}

void loop()
{
    // en: Read slowly on purpose: without flow control a long paste would overflow the buffer.
    // ja: わざとゆっくり読む。フロー制御が無ければ、長い貼り付けでバッファがあふれる。
    if (Serial.available()) {
        const int c = Serial.read();
        Serial.write(c);
        delay(5);
    }
}
