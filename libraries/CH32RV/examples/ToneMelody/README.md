# ToneMelody

[日本語](README.ja.md)

Play a scale on a passive buzzer by specifying frequency and duration.

## Hardware and connections

Connect a passive buzzer or suitable driver to `BUZZER`. Do not drive a load exceeding GPIO current capability directly.

## Steps and expected results

The scale plays once at startup. Reset to repeat. An active buzzer does not follow the requested pitches.

## API and processing flow

`tone(pin, frequency, duration)` takes Hz and ms. The timer continues playback after the call returns; `noTone(pin)` stops it.

## Adapting the example

The delays schedule the next note. Replace them with time-based state when handling other inputs. Check [timer sharing (Japanese)](../../../../docs/timer-design.ja.md) when combining PWM or other timer users.

## Related documentation

- [Sketch](ToneMelody.ino)
- [Basic example setup and index](../README.md)
- [Example guide](../../../../docs/examples.md)
