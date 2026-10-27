# NetworkTime

[日本語](README.ja.md)

Set the clock from an NTP server and print the local time every second. Until the first answer the sketch says the time is not set, instead of printing a 1970 date.

## Operation

After the address, `Time not set yet` may appear for a moment, then lines like `2026-10-27 13:40:02 JST`. Change `TZ_INFO` for your time zone (`UTC0`, `CET-1CEST,M3.5.0,M10.5.0/3`, ...). Without access to the NTP server the message stays.

## API and adaptation

`configTzTime(tz, server)` (or `configTime(gmtOffset, dstOffset, server)`) starts SNTP and returns at once; the clock is set by the first answer and again every hour. `getLocalTime(&tm, 0)` does not wait and is false until then; with a timeout it waits up to that many milliseconds. After that, `time()`, `localtime_r()` and `strftime()` work as usual. The clock is kept by `millis()` between syncs and is lost at reset.

## Before running

Select a board with an Ethernet MAC (the generated profile is CH32V307; V208, V317, V407 / V467 and H415-H417 work the same way) and connect its RJ45 to a network with a DHCP server. Log output is hardware `Serial` at 115200 baud, 8N1: connect adapter RX to board TX, adapter TX to board RX, and common GND. The board's USB connector does not carry this UART.

The network runs inside `delay()` and after each `loop()`; the sketch never calls a poll function. A successful build does not verify the cable or the network. See [build rules](../../../../docs/examples-build-rules.ja.md) and [upload routes](../../../../docs/upload-routes.ja.md).
