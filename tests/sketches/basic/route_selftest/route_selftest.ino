/* setRoute() / setPins() without any wiring.
 *
 * The interesting case tests itself: the sketch moves Serial onto its second
 * route, where nothing is listening, and comes back. Everything printed while
 * it was away is lost by definition, so the lines that do arrive are the proof
 * that it returned - and if it never returns, the test fails by silence.
 *
 * The rejection cases need no bus and no wiring at all: a route number that
 * does not exist, and a TX/RX pair taken from two different routes, both have
 * to come back false with nothing changed.
 */
#include <ch32_route.h>

#include "testcmd.h"

/* The route table of whichever USART is the monitor. Generated per variant. */
#if CH32_SERIAL_DEFAULT == 1 && defined(CH32_SERIAL1_ROUTES)
#define MONITOR_ROUTES      CH32_SERIAL1_ROUTES
#define MONITOR_ROUTE_COUNT CH32_SERIAL1_ROUTE_COUNT
#elif CH32_SERIAL_DEFAULT == 2 && defined(CH32_SERIAL2_ROUTES)
#define MONITOR_ROUTES      CH32_SERIAL2_ROUTES
#define MONITOR_ROUTE_COUNT CH32_SERIAL2_ROUTE_COUNT
#elif CH32_SERIAL_DEFAULT == 3 && defined(CH32_SERIAL3_ROUTES)
#define MONITOR_ROUTES      CH32_SERIAL3_ROUTES
#define MONITOR_ROUTE_COUNT CH32_SERIAL3_ROUTE_COUNT
#elif CH32_SERIAL_DEFAULT == 4 && defined(CH32_SERIAL4_ROUTES)
#define MONITOR_ROUTES      CH32_SERIAL4_ROUTES
#define MONITOR_ROUTE_COUNT CH32_SERIAL4_ROUTE_COUNT
#endif

static void run_checks()
{
    /* 1. A route number no series has. */
    tc_check("unknown_route_refused", !Serial.setRoute(200));

    /* 2. Still alive after a refused call: nothing was half-applied. */
    tc_check("alive_after_refusal", true);

#ifdef MONITOR_ROUTES
    static const ch32_route_t routes[] = MONITOR_ROUTES;
    const uint8_t count = MONITOR_ROUTE_COUNT;

    /* 3. The pins of the route it is already on are accepted. */
    tc_check("current_pins_accepted",
             Serial.setPins(routes[0].pins[0], routes[0].pins[1]));

    if (count >= 2) {
        /* 4. TX from one route and RX from another is not something the
         *    hardware can do, so it has to be refused rather than half-done. */
        tc_check("mixed_route_refused",
                 !Serial.setPins(routes[0].pins[0], routes[1].pins[1]));

        /* 5. Away and back. The print in between goes to pins nobody is
         *    watching; arriving here at all is what is being tested.
         *
         *    The monitor is also where commands arrive, so anything the host
         *    sends during this window is lost too. That is why the host waits
         *    for the done line rather than pipelining another command. */
        const bool went = Serial.setRoute(routes[1].route);
        Serial.println("this line goes nowhere");
        const bool back = Serial.setRoute(routes[0].route);
        tc_check("moved_to_second_route", went);
        tc_check("returned_to_first_route", back);
    } else {
        static const char *const WHY = "one route only";
        tc_skip("mixed_route_refused", WHY);
        tc_skip("moved_to_second_route", WHY);
        tc_skip("returned_to_first_route", WHY);
    }
#else
    static const char *const WHY = "no route table";
    tc_skip("current_pins_accepted", WHY);
    tc_skip("mixed_route_refused", WHY);
    tc_skip("moved_to_second_route", WHY);
    tc_skip("returned_to_first_route", WHY);
#endif

    tc_done();
}

void setup()
{
    tc_begin("route_selftest");
}

void loop()
{
    const char *cmd = tc_ready();
    if (!cmd) {
        return;
    }
    if (!strcmp(cmd, "RUN")) {
        run_checks();
    } else {
        tc_unknown(cmd);
    }
}
