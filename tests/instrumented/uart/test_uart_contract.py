"""uart.external-round-trip: default USART route, 115200 8N1, binary 64-byte payload; then stdio on
the same port: puts() reaches Serial once it is begun, ch32rv_set_stdout(nullptr) discards, and
ch32rv_set_stdout(&Serial) brings it back."""
import secrets


def test_uart_contract(dut, ch32_uart):
    uart = ch32_uart.open(115200)
    payload = bytes([0, 255, 85, 170]) + secrets.token_bytes(60)
    dut.write("uart\n")
    dut.expect_exact("UART READY", timeout=15)
    uart.write(payload)
    uart.expect_exact(payload, timeout=10)
    dut.expect_exact("UART COUNT 64", timeout=10)
    dut.write("stdio\n")
    dut.expect_exact("STDIO DONE", timeout=10)
    m = uart.expect(rb"(?s)STDIO SERIAL\r?\n(.*?)STDIO BACK", timeout=10)
    assert b"DISCARDED" not in m.group(1), m.group(1)
