// en: Fade an LED up and down on any pin, by software PWM (SoftPWM).
// ja: どの端子でも、ソフトウェアの PWM で LED を明るく・暗くする（SoftPWM）。

#include <SoftPWM.h>

// en: Change this to the pin your LED is on.
// ja: LED をつないだ端子に合わせて変える。
#ifdef LED_BUILTIN
static const int LED = LED_BUILTIN;
#else
static const int LED = PA0;
#endif

void setup()
{
    SoftPWMBegin();
    SoftPWMSetFadeTime(LED, 1000, 1000);   // ms from off to full and back
}

void loop()
{
    SoftPWMSet(LED, 255);
    delay(1500);
    SoftPWMSet(LED, 0);
    delay(1500);
}
