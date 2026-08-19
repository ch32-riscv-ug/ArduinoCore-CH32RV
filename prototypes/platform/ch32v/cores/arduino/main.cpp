/* W-3 prototype entry: crt0_ch32.S jumps to main via mret.
 * TODO(Q-012/own crt): run .init_array constructors before setup(). */
#include "Arduino.h"

int main(void)
{
    setup();
    for (;;) {
        loop();
    }
}
