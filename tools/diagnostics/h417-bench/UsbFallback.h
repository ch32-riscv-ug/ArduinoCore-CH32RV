/* Bench only (H417 experiments): an experimental USBSS build may never get
 * configured - no link, an enumeration the host rejects. Then the sketch
 * leaves USBSS and comes back on USBFS (PA11 / PA12), where the DFU runtime
 * rewrites the chip with no probe and no replug. Unconfigured on USBFS too,
 * it tries USBSS again, back and forth.
 *
 * Also a command on the UART (Serial; open it on PB6 TX / PB7 RX, the pins
 * the bench's WCH-LinkE UART is wired to) or on any CDC port fed to
 * command():
 *   'f'  move to USBFS     's'  move to USBSS
 *   'p'  answer "pong <controller> <mounted|unmounted> <ms since boot> ..."
 *        on the port the command came from - is this UART alive? uart-rx
 *        counts the bytes the UART received: it grows by more than the
 *        command when replies come back.
 * Not part of the core: copy this file next to an experiment sketch
 * (docs/h417-bench.ja.md). */
#pragma once

#include <CH32RVUsbDevice.h>

class UsbFallback {
public:
    explicit UsbFallback(CH32RVUsbDevice &device, uint32_t ms = 5000) : _d(device), _ms(ms) {}

    bool begin(CH32RVUsbDeviceConfig config = CH32RVUsbDeviceConfig())
    {
        _cfg = config;
        _cfg.controller = CH32RVUsbController::SuperSpeed;
        _waiting = false;
        _began = _d.begin(_cfg);
        return _began;
    }

    /* From loop(). */
    void poll()
    {
        while (Serial.available()) {
            _uartRx++;
            _from = 'u';
            const int c = Serial.read();
            if (onUart) {
                onUart(c);
            }
            command(c, Serial, _uartLine);
            _from = 'c';
        }
        if (_d.mounted()) {
            _waiting = false;
            return;
        }
        /* A flag, not a nonzero timestamp: millis() | 1 can be a millisecond
         * ahead, and the difference then wraps into an instant timeout. */
        if (!_waiting) {
            _waiting = true;
            _since = millis();
        } else if ((uint32_t)(millis() - _since) >= _ms) {
            note('t');
            moveTo(onSuperSpeed() ? CH32RVUsbController::FullSpeed : CH32RVUsbController::SuperSpeed);
        }
    }

    /* What a port has received of the current line. */
    struct Line {
        char c = 0;
        uint8_t n = 0;
    };

    void command(int c, Print &reply, Line &line)
    {
        if (c != '\r' && c != '\n') {
            line.c = (char)c;
            if (line.n < 255) {
                line.n++;
            }
            return;
        }
        const bool one = line.n == 1;
        c = line.c;
        line = Line();
        if (!one) {
            return;
        }
        if (c == 'f' || c == 's') {
            note(_from);
            note((char)c);
        }
        if (c == 'p') {
            reply.print("pong ");
            reply.print(onSuperSpeed() ? "ss " : "fs ");
            reply.print(_d.mounted() ? "mounted " : "unmounted ");
            reply.print(millis());
            reply.print(" since ");
            reply.print(_waiting ? (uint32_t)(millis() - _since) : 0);
            reply.print(" moves ");
            reply.print(_moves);
            reply.print(" began ");
            reply.print(_began ? "ok" : _d.lastErrorName());
            reply.print(" started ");
            reply.print(_d.started() ? 1 : 0);
            reply.print(" uart-rx ");
            reply.print(_uartRx);
            reply.print(" log ");
            for (uint8_t i = 0; i < sizeof _log; i++) {
                const char ch = _log[(uint8_t)(_logAt + i) % sizeof _log];
                if (ch) {
                    reply.print(ch);
                }
            }
            reply.println();
        } else if (c == 'f') {
            moveTo(CH32RVUsbController::FullSpeed);
        } else if (c == 's') {
            moveTo(CH32RVUsbController::SuperSpeed);
        }
    }

    void (*onUart)(int c) = nullptr;   /* every UART byte, for a sketch's own commands */

    bool onSuperSpeed() const { return _cfg.controller == CH32RVUsbController::SuperSpeed; }

private:
    void note(char c)
    {
        _log[_logAt] = c;
        _logAt = (uint8_t)((_logAt + 1) % sizeof _log);
    }

    void moveTo(CH32RVUsbController controller)
    {
        _d.end();
        for (uint32_t t0 = millis(); (uint32_t)(millis() - t0) < 50u;) {
            /* long enough for the host to see it go */
        }
        _cfg.controller = controller;
        _waiting = false;
        _moves++;
        _began = _d.begin(_cfg);
    }

    CH32RVUsbDevice &_d;
    CH32RVUsbDeviceConfig _cfg;
    uint32_t _ms, _since = 0, _moves = 0;
    bool _began = false, _waiting = false;
    uint32_t _uartRx = 0;
    Line _uartLine;
    char _from = 'c';
    char _log[24] = {};
    uint8_t _logAt = 0;
};
