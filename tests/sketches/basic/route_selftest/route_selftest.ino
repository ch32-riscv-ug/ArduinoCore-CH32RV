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

static int failures;

static void check(const char *name, bool ok)
{
    Serial.print(name);
    Serial.println(ok ? " PASS" : " FAIL");
    if (!ok) {
        failures++;
    }
}

void setup()
{
    Serial.begin(115200);
    while (!Serial) {
    }
    delay(50);
    Serial.println("route_selftest start");

    /* 1. A route number no series has. */
    check("unknown_route_refused", !Serial.setRoute(200));

    /* 2. Still alive after a refused call: nothing was half-applied. */
    check("alive_after_refusal", true);

#ifdef MONITOR_ROUTES
    static const ch32_route_t routes[] = MONITOR_ROUTES;
    const uint8_t count = MONITOR_ROUTE_COUNT;

    /* 3. The pins of the route it is already on are accepted. */
    check("current_pins_accepted",
          Serial.setPins(routes[0].pins[0], routes[0].pins[1]));

    if (count >= 2) {
        /* 4. TX from one route and RX from another is not something the
         *    hardware can do, so it has to be refused rather than half-done. */
        check("mixed_route_refused",
              !Serial.setPins(routes[0].pins[0], routes[1].pins[1]));

        /* 5. Away and back. The print in between goes to pins nobody is
         *    watching; arriving here at all is what is being tested. */
        const bool went = Serial.setRoute(routes[1].route);
        Serial.println("this line goes nowhere");
        const bool back = Serial.setRoute(routes[0].route);
        check("moved_to_second_route", went);
        check("returned_to_first_route", back);
    } else {
        Serial.println("mixed_route_refused SKIP one route only");
        Serial.println("moved_to_second_route SKIP one route only");
        Serial.println("returned_to_first_route SKIP one route only");
    }
#else
    Serial.println("current_pins_accepted SKIP no route table");
    Serial.println("mixed_route_refused SKIP no route table");
    Serial.println("moved_to_second_route SKIP no route table");
    Serial.println("returned_to_first_route SKIP no route table");
#endif

    Serial.print("route_selftest done failures=");
    Serial.println(failures);
}

void loop()
{
}
