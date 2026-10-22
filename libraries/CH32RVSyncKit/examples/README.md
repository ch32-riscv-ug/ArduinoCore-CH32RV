# CH32RVSyncKit examples

[日本語](README.ja.md)

Examples of the parts that connect interrupts and `loop()`. Each uses one part and shows the try-and-return style. Start with [QueueFromInterrupt](QueueFromInterrupt/README.md) and [CriticalSection](CriticalSection/README.md).

## Boards and connections

Every series, CH32V003 included. Most examples use a push button between `PA1` and GND as the interrupt source; change `BUTTON_PIN` in the `.ino` where `PA1` is missing or taken. The log goes to the hardware UART `Serial` at 115200 baud 8N1.

| Board | UART adapter |
|---|---|
| `TX` | RX |
| `RX` | TX (examples that read input) |
| GND | GND |

## The examples

| Example | What it teaches |
|---|---|
| [QueueFromInterrupt](QueueFromInterrupt/README.md) | An interrupt hands values to `loop()` through a `Queue`. |
| [CriticalSection](CriticalSection/README.md) | Reads several variables an ISR writes as one consistent snapshot. |
| [NotifyCounter](NotifyCounter/README.md) | Counts events without data: the ISR calls `notify()`, `loop()` takes them with `tryTakeAll()`. |
| [NotifyBits](NotifyBits/README.md) | Several kinds of event in one object: each ISR raises a bit, `loop()` asks for any or all of them with `tryWaitBits()`. |
| [BinarySemaphore](BinarySemaphore/README.md) | "It happened" once: the ISR gives, `loop()` takes. |
| [RingBuffer](RingBuffer/README.md) | A fixed-size byte stream: characters typed on the UART are collected in a `Ring` and handled per line. |
| [MessageBuffer](MessageBuffer/README.md) | Messages of different lengths, each kept whole: `trySend()` stores all or nothing, `tryReceive()` returns one whole message. |
| [BlockPool](BlockPool/README.md) | Records too big to copy: the ISR borrows a block from a pool, fills it and sends the pointer through a `Queue`; `loop()` uses it and returns it. |
| [DeadlinePeriod](DeadlinePeriod/README.md) | Two schedules at once without `delay()`: a `Period` blinks an LED and a `Deadline` waits 5 s for UART input. |
| [TryLock](TryLock/README.md) | `loop()` fills a buffer in several steps and an ISR must not touch it meanwhile. |

## Building

```sh
arduino-cli compile --fqbn ch32-riscv-ug:ch32rv:CH32V003 \
  libraries/CH32RVSyncKit/examples/QueueFromInterrupt
```
