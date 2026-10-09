"""uart.external-round-trip: default USART route, 115200 8N1, binary 64-byte payload."""
import secrets


def test_uart_contract(dut, ch32_uart):
    uart = ch32_uart.open(115200)
    payload = bytes([0, 255, 85, 170]) + secrets.token_bytes(60)
    dut.write("uart\n")
    dut.expect_exact("UART READY", timeout=15)
    uart.write(payload)
    uart.expect_exact(payload, timeout=10)
    dut.expect_exact("UART COUNT 64", timeout=10)
