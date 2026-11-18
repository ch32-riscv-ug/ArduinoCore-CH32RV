/* PWM on any pin, by software: the SoftPWM library's API (Brett Hagman's,
 * for AVR), on one of the chip's timers.
 *
 *     SoftPWMBegin();
 *     SoftPWMSet(PA0, 64);                  // 64 / 255 high
 *     SoftPWMSetFadeTime(PA1, 500, 1000);   // ms from 0 to 255 and back
 *     SoftPWMSet(PA1, 255);                 // fades up over 500 ms
 *
 * Every pin runs at SOFTPWM_FREQ (60 Hz unless defined before the build) with
 * 256 steps: the timer's interrupt comes SOFTPWM_FREQ x 256 times a second
 * (15.4 kHz at 60 Hz) and sets each pin, so the edges move by the interrupt's
 * latency (a few us; longer while other interrupts run). The timer is the
 * one timerBegin() gives (CH32RVHwTimer.h, the highest-numbered free one),
 * taken at SoftPWMBegin() and given back by SoftPWMEnd(ALL). */
#pragma once

#include <Arduino.h>

#ifndef SOFTPWM_MAXCHANNELS
#define SOFTPWM_MAXCHANNELS 16
#endif
#ifndef SOFTPWM_FREQ
#define SOFTPWM_FREQ 60
#endif

#define SOFTPWM_NORMAL 0
#define SOFTPWM_INVERTED 1

#ifndef ALL
#define ALL -1
#endif

/* (The pins are int, not the AVR original's int8_t: PC0 and on are 128 and up here.) */
/* Takes the timer; defaultPolarity for the pins set from now on. False (and
 * no PWM) when no timer is free. */
bool SoftPWMBegin(uint8_t defaultPolarity = SOFTPWM_NORMAL);
/* value / 255 high (inverted: low); the pin becomes an output. hardset:
 * at once, past any fade. pin ALL: every pin set so far. */
void SoftPWMSet(int pin, uint8_t value, uint8_t hardset = 0);
void SoftPWMSetPercent(int pin, uint8_t percent, uint8_t hardset = 0);
/* The pin stops (left at its inactive level) and frees its channel; ALL
 * stops every pin and gives the timer back. */
void SoftPWMEnd(int pin);
/* ms for a change from 0 to 255 (up) and from 255 to 0 (down); 0: at once. */
void SoftPWMSetFadeTime(int pin, uint16_t fadeUpTime, uint16_t fadeDownTime);
void SoftPWMSetPolarity(int pin, uint8_t polarity);
