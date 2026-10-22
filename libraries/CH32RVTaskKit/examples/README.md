# CH32RVTaskKit examples

[日本語](README.ja.md)

Short pieces of work run by their triggers. In each, only `runOnce()` in `loop()` runs work.

## Boards and connections

Every series, CH32V003 included. The log goes to the hardware UART `Serial` at 115200 baud 8N1. Change the pins in each `.ino`.

## The examples

| Example | What it teaches |
|---|---|
| [BasicWork](BasicWork/README.md) | Two pieces of work that run only on their trigger: an LED on a `Period`, and a line reader when the UART has bytes. |
| [WorkFromInterrupt](WorkFromInterrupt/README.md) | An interrupt does not do the work, it asks for it: the ISR puts the press time in a `Queue` and calls `post()`; `runOnce()` in `loop()` handles it. |
| [WorkStats](WorkStats/README.md) | Which piece of work is slow? Each Work counts and times its runs and the executor counts its rounds. |
