#include "Wire.h"

#include "Arduino.h"
#include "ch32_gpio.h"
#include "ch32_registers.h"

using namespace arduino;

/* The I2C block hangs off PCLK1, and Milestone 1 leaves both APB prescalers at
 * /1, so PCLK1 is HCLK, which SystemInit makes equal to F_CPU. When a PLL or a
 * non-unity APB prescaler arrives this has to follow (docs/todo.ja.md). */
static const uint32_t CH32_I2C_PCLK1 = F_CPU;

namespace {

/* Clearing ADDR and setting STOP have to happen without an interrupt in
 * between: the peripheral starts clocking the next byte the moment ADDR is
 * cleared, so a SysTick tick landing there costs a byte. Saves and restores
 * rather than blindly enabling, so a caller that already had interrupts off
 * gets them back off. */
class NoIrq {
public:
    NoIrq() { __asm__ volatile ("csrrci %0, mstatus, 8"
                                : "=r"(_saved) :: "memory"); }
    ~NoIrq() {
        if (_saved & 0x8u) {
            __asm__ volatile ("csrsi mstatus, 8" ::: "memory");
        }
    }
private:
    uint32_t _saved;
};

/* STAR1's error flags are cleared by writing a zero to them, unlike the event
 * flags, which clear as a side effect of reading a register. */
inline void clear_errors(uint32_t base)
{
    CH32_I2C_STAR1(base) &= (uint16_t)~(CH32_I2C_STAR1_AF | CH32_I2C_STAR1_BERR |
                                        CH32_I2C_STAR1_ARLO | CH32_I2C_STAR1_OVR);
}

inline void clear_addr(uint32_t base)
{
    (void)CH32_I2C_STAR1(base);
    (void)CH32_I2C_STAR2(base);
}

}  // namespace

bool CH32TwoWire::wait_flag1(uint16_t mask, bool set)
{
    const uint32_t start_us = micros();
    for (;;) {
        const uint16_t s1 = CH32_I2C_STAR1(_base);
        if (((s1 & mask) != 0) == set) {
            return true;
        }
        /* A device that never acknowledges reports AF instead of just never
         * raising the flag being waited on; treating that as "keep waiting"
         * would spend the whole timeout on every missing device. */
        if (s1 & (CH32_I2C_STAR1_AF | CH32_I2C_STAR1_BERR |
                  CH32_I2C_STAR1_ARLO)) {
            return false;
        }
        if (micros() - start_us > CH32_WIRE_TIMEOUT_US) {
            return false;
        }
    }
}

void CH32TwoWire::begin()
{
    CH32_RCC_APB1PCENR |= _clock_bit;
    CH32_RCC_APB2PCENR |= CH32_RCC_APB2_AFIO;
    /* Written every time, default route included - see HardwareSerial::begin
     * for why "leave the field alone" is the wrong default. */
    if (_remap_mask) {
        CH32_AFIO_PCFR1 = (CH32_AFIO_PCFR1 & ~_remap_mask) | _remap_value;
    }
    if (_remap2_mask) {
        CH32_AFIO_PCFR2 = (CH32_AFIO_PCFR2 & ~_remap2_mask) | _remap2_value;
    }

    /* Open drain, because that is what the bus is: both lines are pulled up
     * externally and every device only ever pulls them down. */
    const uint8_t scl_port = (uint8_t)CH32_PIN_PORT(_scl_pin);
    const uint8_t sda_port = (uint8_t)CH32_PIN_PORT(_sda_pin);
    ch32_gpio_clock_enable(scl_port);
    ch32_gpio_clock_enable(sda_port);
    ch32_gpio_set_config(scl_port, (uint8_t)CH32_PIN_BIT(_scl_pin),
                         CH32_GPIO_CFG_AF_OD_50M);
    ch32_gpio_set_config(sda_port, (uint8_t)CH32_PIN_BIT(_sda_pin),
                         CH32_GPIO_CFG_AF_OD_50M);

    /* SWRST is the only way back from a transfer that was interrupted with the
     * bus held: BUSY is otherwise latched and every later transfer fails. */
    CH32_I2C_CTLR1(_base) = CH32_I2C_CTLR1_SWRST;
    CH32_I2C_CTLR1(_base) = 0;

    _started = true;
    _needs_recovery = false;
    setClock(_clock_hz);
}

void CH32TwoWire::begin(uint8_t address)
{
    /* Slave mode is not implemented. Coming up as a master would be worse than
     * doing nothing: the sketch would look alive while never answering. */
    (void)address;
}

void CH32TwoWire::end()
{
    CH32_I2C_CTLR1(_base) = 0;
    CH32_RCC_APB1PCENR &= ~_clock_bit;
    _started = false;
    _transmitting = false;
    _tx_len = 0;
    _rx_len = 0;
    _rx_read = 0;
}

void CH32TwoWire::setClock(uint32_t freq)
{
    _clock_hz = freq ? freq : 100000;
    if (!_started) {
        return;                        /* begin() applies it */
    }
    /* FREQ and CCR may only be written while the peripheral is disabled. */
    CH32_I2C_CTLR1(_base) &= (uint16_t)~CH32_I2C_CTLR1_PE;

    const uint32_t mhz = CH32_I2C_PCLK1 / 1000000u;
    CH32_I2C_CTLR2(_base) = (uint16_t)(mhz & CH32_I2C_CTLR2_FREQ_MASK);

    uint32_t ccr;
    if (_clock_hz <= 100000u) {
        /* Standard mode: the low and high halves of SCL are equal, so one
         * period is 2 x CCR peripheral clocks. */
        ccr = CH32_I2C_PCLK1 / (2u * _clock_hz);
        if (ccr < 4u) {
            ccr = 4u;
        }
        CH32_I2C_CKCFGR(_base) = (uint16_t)(ccr & CH32_I2C_CKCFGR_CCR_MASK);
#if CH32_I2C_HAS_RTR
        CH32_I2C_RTR(_base) = (uint16_t)(mhz + 1u);
#endif
    } else {
        /* Fast mode with the 2:1 duty cycle: 3 x CCR per period. */
        ccr = CH32_I2C_PCLK1 / (3u * _clock_hz);
        if (ccr < 1u) {
            ccr = 1u;
        }
        CH32_I2C_CKCFGR(_base) = (uint16_t)(CH32_I2C_CKCFGR_FS |
                                            (ccr & CH32_I2C_CKCFGR_CCR_MASK));
#if CH32_I2C_HAS_RTR
        CH32_I2C_RTR(_base) = (uint16_t)((mhz * 300u) / 1000u + 1u);
#endif
    }

    CH32_I2C_CTLR1(_base) |= CH32_I2C_CTLR1_PE;
}

void CH32TwoWire::recover(void)
{
    /* Full re-initialisation. Cheap, and the only reliable way out of a
     * latched BUSY - which is exactly the state a sketch reaches by resetting
     * the MCU in the middle of a transfer. */
    CH32_I2C_CTLR1(_base) = CH32_I2C_CTLR1_SWRST;
    CH32_I2C_CTLR1(_base) = 0;
    _needs_recovery = false;
    setClock(_clock_hz);
}

bool CH32TwoWire::start(uint8_t address, bool read)
{
    clear_errors(_base);
    CH32_I2C_CTLR1(_base) |= CH32_I2C_CTLR1_START;
    if (!wait_flag1(CH32_I2C_STAR1_SB, true)) {
        return false;
    }
    CH32_I2C_DATAR(_base) = (uint16_t)((address << 1) | (read ? 1u : 0u));
    return wait_flag1(CH32_I2C_STAR1_ADDR, true);
}

void CH32TwoWire::stop(void)
{
    CH32_I2C_CTLR1(_base) |= CH32_I2C_CTLR1_STOP;
}

void CH32TwoWire::beginTransmission(uint8_t address)
{
    _address = address;
    _tx_len = 0;
    _tx_overflow = false;
    _transmitting = true;
}

uint8_t CH32TwoWire::endTransmission(bool stopBit)
{
    if (!_transmitting) {
        return 4;
    }
    _transmitting = false;
    if (!_started) {
        return 4;
    }
    if (_tx_overflow) {
        _tx_overflow = false;
        _tx_len = 0;
        return 1;                        /* AVR's "data too long" */
    }
    if (_needs_recovery) {
        recover();
    }

    if (!start(_address, false)) {
        /* AF here is the ordinary "nothing at that address" case. */
        const bool nack = (CH32_I2C_STAR1(_base) & CH32_I2C_STAR1_AF) != 0;
        clear_errors(_base);
        stop();
        _needs_recovery = !nack;
        _tx_len = 0;
        return nack ? 2 : 5;
    }
    clear_addr(_base);

    for (uint8_t i = 0; i < _tx_len; i++) {
        if (!wait_flag1(CH32_I2C_STAR1_TXE, true)) {
            const bool nack = (CH32_I2C_STAR1(_base) & CH32_I2C_STAR1_AF) != 0;
            clear_errors(_base);
            stop();
            _needs_recovery = !nack;
            _tx_len = 0;
            return nack ? 3 : 5;
        }
        CH32_I2C_DATAR(_base) = _tx[i];
    }
    /* BTF rather than TXE: TXE only says the shift register took the byte,
     * and stopping there truncates the last one on the wire. */
    if (!wait_flag1(CH32_I2C_STAR1_BTF, true)) {
        const bool nack = (CH32_I2C_STAR1(_base) & CH32_I2C_STAR1_AF) != 0;
        clear_errors(_base);
        stop();
        _needs_recovery = !nack;
        _tx_len = 0;
        return nack ? 3 : 5;
    }

    if (stopBit) {
        stop();
    }
    _tx_len = 0;
    return 0;
}

size_t CH32TwoWire::requestFrom(uint8_t address, size_t len, bool stopBit)
{
    _rx_len = 0;
    _rx_read = 0;
    if (!_started || len == 0) {
        return 0;
    }
    if (len > CH32_WIRE_BUFFER_SIZE) {
        len = CH32_WIRE_BUFFER_SIZE;
    }
    if (_needs_recovery) {
        recover();
    }

    /* ACK has to be right before ADDR is cleared, because the peripheral
     * decides what to do with the first byte at that moment. */
    if (len == 1) {
        CH32_I2C_CTLR1(_base) &= (uint16_t)~CH32_I2C_CTLR1_ACK;
    } else {
        CH32_I2C_CTLR1(_base) |= CH32_I2C_CTLR1_ACK;
    }

    if (!start(address, true)) {
        clear_errors(_base);
        stop();
        _needs_recovery = (CH32_I2C_STAR1(_base) & CH32_I2C_STAR1_AF) == 0;
        return 0;
    }

    size_t got = 0;
    if (len == 1) {
        {
            NoIrq lock;
            clear_addr(_base);
            stop();
        }
        if (wait_flag1(CH32_I2C_STAR1_RXNE, true)) {
            _rx[got++] = (uint8_t)CH32_I2C_DATAR(_base);
        }
    } else if (len == 2) {
        /* POS makes ACK apply to the byte after next, which is what lets both
         * bytes be read out of DR and the shift register at once - the only
         * way to NACK the second byte without also NACKing the first. */
        CH32_I2C_CTLR1(_base) |= CH32_I2C_CTLR1_POS;
        CH32_I2C_CTLR1(_base) &= (uint16_t)~CH32_I2C_CTLR1_ACK;
        {
            NoIrq lock;
            clear_addr(_base);
        }
        if (wait_flag1(CH32_I2C_STAR1_BTF, true)) {
            NoIrq lock;
            stop();
            _rx[got++] = (uint8_t)CH32_I2C_DATAR(_base);
            _rx[got++] = (uint8_t)CH32_I2C_DATAR(_base);
        }
        CH32_I2C_CTLR1(_base) &= (uint16_t)~CH32_I2C_CTLR1_POS;
    } else {
        clear_addr(_base);
        while (len - got > 3) {
            if (!wait_flag1(CH32_I2C_STAR1_RXNE, true)) {
                break;
            }
            _rx[got++] = (uint8_t)CH32_I2C_DATAR(_base);
        }
        /* Last three: with two bytes still in DR and the shift register, the
         * NACK has to be armed before the third one is clocked in. */
        if (len - got == 3 && wait_flag1(CH32_I2C_STAR1_BTF, true)) {
            CH32_I2C_CTLR1(_base) &= (uint16_t)~CH32_I2C_CTLR1_ACK;
            {
                NoIrq lock;
                _rx[got++] = (uint8_t)CH32_I2C_DATAR(_base);
                stop();
            }
            _rx[got++] = (uint8_t)CH32_I2C_DATAR(_base);
            if (wait_flag1(CH32_I2C_STAR1_RXNE, true)) {
                _rx[got++] = (uint8_t)CH32_I2C_DATAR(_base);
            }
        }
    }

    if (got != len) {
        clear_errors(_base);
        stop();
        _needs_recovery = true;
    } else if (!stopBit) {
        /* Arduino's repeated-start form. The bus is left owned, so the next
         * call must not wait for it to go idle. */
    }

    CH32_I2C_CTLR1(_base) |= CH32_I2C_CTLR1_ACK;
    _rx_len = (uint8_t)got;
    return got;
}

void CH32TwoWire::onReceive(void (*callback)(int))
{
    (void)callback;      /* slave mode is not implemented */
}

void CH32TwoWire::onRequest(void (*callback)(void))
{
    (void)callback;      /* slave mode is not implemented */
}

size_t CH32TwoWire::write(uint8_t data)
{
    if (!_transmitting) {
        return 0;        /* slave mode is not implemented */
    }
    if (_tx_len >= CH32_WIRE_BUFFER_SIZE) {
        /* AVR truncates and reports it from endTransmission() as 1, so the
         * flag has to survive until then. */
        _tx_overflow = true;
        return 0;
    }
    _tx[_tx_len++] = data;
    return 1;
}

size_t CH32TwoWire::write(const uint8_t *data, size_t len)
{
    size_t written = 0;
    for (size_t i = 0; i < len; i++) {
        if (!write(data[i])) {
            break;
        }
        written++;
    }
    return written;
}

int CH32TwoWire::available(void)
{
    return _rx_len - _rx_read;
}

int CH32TwoWire::read(void)
{
    if (_rx_read >= _rx_len) {
        return -1;
    }
    return _rx[_rx_read++];
}

int CH32TwoWire::peek(void)
{
    if (_rx_read >= _rx_len) {
        return -1;
    }
    return _rx[_rx_read];
}

/* ------------------------------------------------------- instances */
/* Naming follows the Arduino ecosystem rather than this core's Serial: the
 * bare name is the first bus and Wire1 is the second, which is what Due, Zero,
 * STM32duino and arduino-pico all do. A library asking for Wire1 means "the
 * other one", not "I2C1". */
#ifndef CH32_I2C1_REMAP_MASK
#define CH32_I2C1_REMAP_MASK 0u
#define CH32_I2C1_REMAP_VAL  0u
#endif
#ifndef CH32_I2C1_REMAP2_MASK
#define CH32_I2C1_REMAP2_MASK 0u
#define CH32_I2C1_REMAP2_VAL  0u
#endif
#ifndef CH32_I2C2_REMAP_MASK
#define CH32_I2C2_REMAP_MASK 0u
#define CH32_I2C2_REMAP_VAL  0u
#endif
#ifndef CH32_I2C2_REMAP2_MASK
#define CH32_I2C2_REMAP2_MASK 0u
#define CH32_I2C2_REMAP2_VAL  0u
#endif

#if defined(CH32_I2C1_SCL)
arduino::CH32TwoWire Wire(CH32_I2C1_BASE, CH32_RCC_APB1_I2C1,
                          CH32_I2C1_SCL, CH32_I2C1_SDA,
                          CH32_I2C1_REMAP_MASK, CH32_I2C1_REMAP_VAL,
                          CH32_I2C1_REMAP2_MASK, CH32_I2C1_REMAP2_VAL);
#if defined(CH32_I2C2_SCL)
arduino::CH32TwoWire Wire1(CH32_I2C2_BASE, CH32_RCC_APB1_I2C2,
                           CH32_I2C2_SCL, CH32_I2C2_SDA,
                           CH32_I2C2_REMAP_MASK, CH32_I2C2_REMAP_VAL,
                           CH32_I2C2_REMAP2_MASK, CH32_I2C2_REMAP2_VAL);
#endif
#elif defined(CH32_I2C2_SCL)
/* A part that bonds only the second instance still gets a plain Wire. */
arduino::CH32TwoWire Wire(CH32_I2C2_BASE, CH32_RCC_APB1_I2C2,
                          CH32_I2C2_SCL, CH32_I2C2_SDA,
                          CH32_I2C2_REMAP_MASK, CH32_I2C2_REMAP_VAL,
                          CH32_I2C2_REMAP2_MASK, CH32_I2C2_REMAP2_VAL);
#endif
