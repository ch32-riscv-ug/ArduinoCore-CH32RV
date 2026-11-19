# DmaCopy

[日本語](README.ja.md)

The CPU keeps counting while DMA copies 256 bytes; the copy's end comes by the callback, and the data is checked. Then
a fill is checked.

## What you need and the wiring

Any part. No wiring.

## What to do and what to expect

`Serial` shows something like this (the DMA and channel numbers and the count differ by part):

```
DMA1 channel 7
copy: same, the CPU counted to 41
fill: all 0xA5
```

## The API and the flow

`begin()` takes a free channel, `onDone()` sets the callback for the end, and `copyAsync()` starts the copy and
returns at once. The sketch counts until the callback sets a flag, then checks with `memcmp()`. `fill()` is the waiting
form.
