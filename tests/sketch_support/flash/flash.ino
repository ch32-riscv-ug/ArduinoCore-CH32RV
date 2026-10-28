/* ch32rv_flash contracts (tests/instrumented/flash): every function and every
 * refusal on a scratch store, a value kept across a software reset, and how
 * one rewrite_page() treats the CPU. No wiring. About 16 erases of the stores
 * at the end of flash per run. Console on SerialDMSeq:
 *
 *   ready <nonce>  READY <nonce> <build id> <erase size> <write size> <erased word, hex>
 *   units          UNITS <steps> <failed-step bitmap, hex>
 *   save <value>   SAVE <0/1>: <value> into the kept store, rewrite_page()
 *   restart        CH32RV.restart(); then
 *   boot           BOOT <0/1> <value>: what setup() found in the kept store
 *   time           TIME <us> <millis advance> <micros advance>: one rewrite_page()
 *                  timed by the SysTick counter with interrupts off, then the
 *                  clock's advance across one with them on */
#include <Arduino.h>
#include <CH32RV.h>
#include <SerialDMSeq.h>
#include <ch32rv_flash.h>
#include <ch32rv_irq.h>
#include <ch32rv_registers.h>
#include <stdlib.h>
#include <string.h>

#ifndef BENCH_BUILD_ID
#error BENCH_BUILD_ID must identify this compilation
#endif

#define EW (CH32RV_FLASH_ERASE_SIZE / 4u)
#define WW (CH32RV_FLASH_WRITE_SIZE / 4u)
#define KEPT_MAGIC 0x4B455054u

CH32RV_FLASH_STORE(scratch, 2 * CH32RV_FLASH_ERASE_SIZE);
CH32RV_FLASH_STORE(kept, CH32RV_FLASH_ERASE_SIZE);

static uint32_t buf[WW + 1];
static bool bootFound;
static uint32_t bootValue;

static const volatile uint32_t *scratchWord(uint32_t i)
{
    return (const volatile uint32_t *)scratch + i;
}

static bool erased(uint32_t from, uint32_t n)
{
    for (uint32_t i = from; i < from + n; i++) {
        if (*scratchWord(i) != CH32RV_FLASH_ERASED_WORD) {
            return false;
        }
    }
    return true;
}

static void fill(uint32_t seed)
{
    for (uint32_t i = 0; i < WW + 1; i++) {
        buf[i] = seed * 0x9E3779B1u + i * 0x01010101u;
    }
}

/* The WW words at word index `at` against src (any alignment). */
static bool same(uint32_t at, const uint8_t *src)
{
    for (uint32_t i = 0; i < WW; i++) {
        uint32_t w;
        memcpy(&w, src + 4u * i, 4);
        if (*scratchWord(at + i) != w) {
            return false;
        }
    }
    return true;
}

static uint32_t steps, fails;

static void step(bool ok)
{
    if (!ok) {
        fails |= 1u << steps;
    }
    steps++;
}

static void units()
{
    const uint32_t off = CH32RV_FLASH_OFFSET(scratch);
    steps = fails = 0;
    step(ch32rv_flash_erase_page(off));
    step(erased(0, EW));
    fill(1);
    step(ch32rv_flash_write_page(off, buf));
    step(same(0, (const uint8_t *)buf));
    fill(2);
    step(ch32rv_flash_rewrite_page(off, buf));
    step(same(0, (const uint8_t *)buf) && erased(WW, EW - WW));
    step(ch32rv_flash_erase(off, 2 * CH32RV_FLASH_ERASE_SIZE));
    step(erased(0, 2 * EW));
    fill(3);
    step(ch32rv_flash_write(off + CH32RV_FLASH_ERASE_SIZE, (const uint8_t *)buf + 1, CH32RV_FLASH_WRITE_SIZE));
    step(same(EW, (const uint8_t *)buf + 1));
    fill(4);
    step(ch32rv_flash_erase_page(off + CH32RV_FLASH_ERASE_SIZE) &&
         ch32rv_flash_write_page(off + 2 * CH32RV_FLASH_ERASE_SIZE - CH32RV_FLASH_WRITE_SIZE, buf));
    step(same(2 * EW - WW, (const uint8_t *)buf) && erased(EW, EW - WW));
    /* Refused: the program itself (where it runs from flash), unaligned, past FLASH, empty. */
#if CH32RV_FLASH_SELF_WRITE
    step(true);
#else
    step(!ch32rv_flash_erase_page(0));
#endif
    step(!ch32rv_flash_erase_page(off + 4));
    step(!ch32rv_flash_erase(off, 3 * CH32RV_FLASH_ERASE_SIZE + 0x100000u));
    step(!ch32rv_flash_write(off, buf, 0));
    SerialDMSeq.print("UNITS ");
    SerialDMSeq.print((unsigned long)steps);
    SerialDMSeq.print(' ');
    SerialDMSeq.println((unsigned long)fails, HEX);
}

static void save(uint32_t value)
{
    for (uint32_t i = 0; i < WW; i++) {
        buf[i] = 0xFFFFFFFFu;
    }
    buf[0] = KEPT_MAGIC;
    buf[1] = value;
    buf[2] = ~value;
    SerialDMSeq.print("SAVE ");
    SerialDMSeq.println(ch32rv_flash_rewrite_page(CH32RV_FLASH_OFFSET(kept), buf) ? 1 : 0);
}

static void timeOne()
{
    const uint32_t off = CH32RV_FLASH_OFFSET(scratch);
    fill(5);
    const uint32_t irq = ch32rv_irq_save();
    const uint32_t c0 = CH32RV_SYSTICK_CNT;
    ch32rv_flash_rewrite_page(off, buf);
    const uint32_t c1 = CH32RV_SYSTICK_CNT;
    ch32rv_irq_restore(irq);
    delay(2);
    const uint32_t m0 = millis(), u0 = micros();
    ch32rv_flash_rewrite_page(off, buf);
    const uint32_t u1 = micros(), m1 = millis();
    SerialDMSeq.print("TIME ");
    SerialDMSeq.print((unsigned long)((c1 - c0) / (F_CPU / 1000000u)));
    SerialDMSeq.print(' ');
    SerialDMSeq.print((unsigned long)(m1 - m0));
    SerialDMSeq.print(' ');
    SerialDMSeq.println((unsigned long)(u1 - u0));
}

static char line[48];
static uint8_t used;

void setup()
{
    const uint32_t *k = (const uint32_t *)kept;
    bootFound = k[0] == KEPT_MAGIC && k[2] == ~k[1];
    bootValue = bootFound ? k[1] : 0;
    SerialDMSeq.begin(115200);
}

void loop()
{
    while (SerialDMSeq.available()) {
        const int ch = SerialDMSeq.read();
        if (ch != '\n') {
            if (ch != '\r' && used < sizeof line - 1) {
                line[used++] = (char)ch;
            }
            continue;
        }
        line[used] = 0;
        used = 0;
        if (strncmp(line, "ready ", 6) == 0) {
            SerialDMSeq.print("READY ");
            SerialDMSeq.print(line + 6);
            SerialDMSeq.print(' ');
            SerialDMSeq.print(BENCH_BUILD_ID);
            SerialDMSeq.print(' ');
            SerialDMSeq.print((unsigned long)CH32RV_FLASH_ERASE_SIZE);
            SerialDMSeq.print(' ');
            SerialDMSeq.print((unsigned long)CH32RV_FLASH_WRITE_SIZE);
            SerialDMSeq.print(' ');
            SerialDMSeq.println((unsigned long)CH32RV_FLASH_ERASED_WORD, HEX);
        } else if (strcmp(line, "units") == 0) {
            units();
        } else if (strncmp(line, "save ", 5) == 0) {
            save(strtoul(line + 5, nullptr, 10));
        } else if (strcmp(line, "restart") == 0) {
            CH32RV.restart();
        } else if (strcmp(line, "boot") == 0) {
            SerialDMSeq.print("BOOT ");
            SerialDMSeq.print(bootFound ? 1 : 0);
            SerialDMSeq.print(' ');
            SerialDMSeq.println((unsigned long)bootValue);
        } else if (strcmp(line, "time") == 0) {
            timeOne();
        } else if (line[0]) {
            SerialDMSeq.println("ERROR COMMAND");
        }
    }
}
