// en: Take a 1 Mbaud stream while loop() is busy elsewhere: the receive buffer is filled by DMA, not one interrupt per byte.
// ja: loop()がほかの仕事をしている間も1 Mbaudの流れを受け取る。受信バッファはDMAが埋める（1バイトごとの割り込みではない）。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

static uint32_t received;
static uint32_t checksum;
static volatile uint8_t errors;
static uint32_t lastReport;

static void onError(hardwareSerial_error_t kind)
{
    errors |= (uint8_t)(1u << kind);
}

void setup()
{
    // en: Before begin(): 512 bytes hold 5 ms at 1 Mbaud. The built-in 64 would hold 0.6 ms.
    // ja: begin()の前に。512バイトは1 Mbaudで5 ms分。既定の64では0.6 ms分。
    Serial.setRxBufferSize(512);
    Serial.setTxBufferSize(256);
    Serial.begin(1000000);
    Serial.onReceiveError(onError);
    Serial.println("SerialDmaBuffers ready - send a file at 1000000 8N1");
}

void loop()
{
    // en: Something slow: the bytes keep arriving in the buffer meanwhile.
    // ja: 時間のかかる仕事。その間もバイトはバッファに届き続ける。
    delay(4);

    while (Serial.available()) {
        checksum += (uint8_t)Serial.read();
        ++received;
    }

    if (millis() - lastReport >= 1000) {
        lastReport = millis();
        Serial.print("received ");
        Serial.print(received);
        Serial.print(" bytes, sum ");
        Serial.println(checksum);
        if (errors) {
            // en: bit 2 UART_BUFFER_FULL_ERROR: the loop was away longer than the buffer holds.
            // ja: ビット2 UART_BUFFER_FULL_ERROR: バッファが持つ時間よりloop()が長く離れた。
            Serial.print("receive error mask 0x");
            Serial.println(errors, HEX);
            errors = 0;
        }
    }
}
