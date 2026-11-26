# InterruptPriority

[日本語](README.ja.md)

Set a peripheral interrupt's priority so that the core's interrupts (here SysTick, which drives `millis()`) can interrupt a slow handler.

## Hardware and connections

Connect a UART monitor.

## Steps and expected results

Every half second a timer handler busy-waits for a few milliseconds and the sketch prints how far `millis()` moved meanwhile. Four lines at `level 0` (the core's level: SysTick waits, `0 ms`) alternate with four at `level 1` (less urgent: SysTick interrupts the handler, a few ms). On a chip whose nesting is not working the second group stays at 0.

## API and processing flow

`ch32rv_priority.h` sets the PFIC's priority byte: `ch32rv_irq_set_priority(irqn, priority)`, smaller is more urgent. `CH32RV_IRQ_PRIORITY(level)` is the byte of a preemption level, 0 to `CH32RV_IRQ_PREEMPT_LEVELS - 1` (2 levels on most series, 4 on V30x and M030, 8 on H41x's V5F). A timer's interrupt number is its capability's `update_irq`; a pin's is `ch32rv_pin_irqn(pin)`; others are `CH32RV_IRQN_<name>`. The core leaves every interrupt, its own included, at 0 (the most urgent); a handler set less urgent is interrupted by the core's.

## Adapting the example

A handler that can be interrupted by another must not call the driver whose interrupt that is (for example `Serial` from a handler the USART's interrupt can interrupt, after lowering the USART's). The CH32RV library README has the full contract.

## Related documentation

- [Sketch](InterruptPriority.ino)
- [Basic example setup and index](../../README.md)
- [Example guide](../../../../../docs/examples.md)
- [Sketch composition guide (Japanese)](../../../../../docs/sketch-guide.ja.md)
