/* pd_frames.h - USB Power Delivery message logic, and nothing else.
 *
 * Pure functions over integers: no registers, no Arduino.h, no allocation.
 * The hardware driver feeds received words in and sends the returned words
 * out; tests feed the same words on a desktop. That split is the point: the
 * part of USB PD that is easy to get subtly wrong - bitfield layouts and unit
 * conversions - is exactly the part that needs no hardware to verify, so it
 * lives where a unit test can reach it. The same checks run twice:
 *
 *   tests/unit/test_pd_frames.py          host, via a shared object + ctypes
 *   tests/sketches/basic/pd_selftest      on the target, over the command
 *                                         protocol - proves the shifts on
 *                                         rv32ec too, with no PD hardware
 *
 * Bit numbers in the comments are the USB PD R3.1 spec's. The layouts were
 * cross-checked against two independent implementations - WCH's EVT
 * USBPD_SNK example and wagiminator's CH32X035-USB-PD-Adapter - as reference
 * only; no code was taken from either (the latter is CC BY-SA).
 *
 * Everything is millivolts and milliamps at the API. The spec's five
 * different units (10 mA, 50 mA, 50 mV, 100 mV, 20 mV, 250 mW) exist only
 * inside these functions, which is where a unit mistake becomes a test
 * failure instead of a smoking board.
 */
#ifndef PD_FRAMES_H
#define PD_FRAMES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A Source_Capabilities message carries at most 7 data objects (the header's
 * count field is 3 bits). EPR raises that, over a different message; out of
 * scope here. */
#define PD_PDO_MAX 7

/* What kind of supply one PDO advertises. */
typedef enum {
    PD_SUPPLY_FIXED = 0,     /* one voltage, e.g. "9 V at up to 3 A"        */
    PD_SUPPLY_PPS,           /* adjustable range in 20 mV steps (the APDO)  */
    PD_SUPPLY_BATTERY,       /* a voltage range with a power cap            */
    PD_SUPPLY_VARIABLE,      /* a voltage range, unregulated, not settable  */
    PD_SUPPLY_UNKNOWN,       /* an APDO this library does not speak (AVS)   */
} pd_supply_t;

/* One profile, decoded. min_mv == max_mv for a fixed supply. max_mw is only
 * meaningful for PD_SUPPLY_BATTERY, whose current depends on the voltage.
 * `raw` keeps the word as received, for logging and for bug reports. */
typedef struct {
    uint8_t  kind;           /* pd_supply_t */
    uint16_t min_mv;
    uint16_t max_mv;
    uint16_t max_ma;         /* 0 for battery PDOs */
    uint32_t max_mw;         /* 0 except for battery PDOs */
    uint32_t raw;
} pd_pdo_t;

/* Everything a Source_Capabilities message says. The three flags come from
 * the first PDO, which the spec requires to be the 5 V fixed one. */
typedef struct {
    uint8_t count;
    uint8_t usb_comm;        /* source can talk USB data          (B26) */
    uint8_t unconstrained;   /* source is not sharing its budget  (B27) */
    uint8_t dual_role_power; /* source can also be a sink         (B29) */
    pd_pdo_t pdo[PD_PDO_MAX];
} pd_caps_t;

/* ---- message header (16 bits) --------------------------------------------
 * Only what a sink needs. pd_header() builds sink/UFP headers (both role bits
 * zero); the accessors work on anything received. */

/* Control messages (no data objects). */
#define PD_CTRL_GOODCRC         0x01u
#define PD_CTRL_ACCEPT          0x03u
#define PD_CTRL_REJECT          0x04u
#define PD_CTRL_PS_RDY          0x06u
#define PD_CTRL_GET_SOURCE_CAP  0x07u
#define PD_CTRL_SOFT_RESET      0x0Du
/* Data messages (count > 0). */
#define PD_DATA_SOURCE_CAP      0x01u
#define PD_DATA_REQUEST         0x02u
#define PD_DATA_VENDOR_DEFINED  0x0Fu
/* Values of the spec-revision field (B7..6). */
#define PD_REV_2_0              0x01u
#define PD_REV_3_0              0x02u

uint16_t pd_header(uint8_t type, uint8_t count, uint8_t message_id,
                   uint8_t rev);
uint8_t pd_header_type(uint16_t header);       /* B4..0            */
uint8_t pd_header_count(uint16_t header);      /* data objects, B14..12 */
uint8_t pd_header_id(uint16_t header);         /* B11..9           */
uint8_t pd_header_rev(uint16_t header);        /* B7..6            */
int     pd_header_extended(uint16_t header);   /* B15              */

/* ---- Source_Capabilities ------------------------------------------------ */

/* Decode `count` PDO words into `out`. Anything malformed becomes
 * PD_SUPPLY_UNKNOWN rather than an error: the source said it, so it is a
 * fact worth showing, just not one worth requesting. */
void pd_parse_source_caps(const uint32_t *pdo, uint8_t count, pd_caps_t *out);

/* Pick the profile to request for want_mv (and want_ma; 0 = any current).
 * Returns an index into caps->pdo, or -1.
 *
 * A fixed profile at exactly want_mv wins over a PPS range containing it: a
 * fixed contract holds without attention, while a PPS contract dies unless
 * re-requested every few seconds (see maintain() in USBPD.h), and a sketch
 * sitting in delay() would lose it. Among fitting PPS ranges the one with the
 * most current wins. Battery and variable profiles are never picked - they
 * are listed for the user, not requested.
 *
 * Voltages between fixed levels only match a PPS range: request(8000) on a
 * 5/9/12 V charger with no PPS returns -1 rather than "9 is close enough". */
int pd_pick(const pd_caps_t *caps, uint16_t want_mv, uint16_t want_ma);

/* ---- Request data objects ------------------------------------------------
 * `position` is the spec's 1-based object position: index + 1.
 * The builders return only position + amounts; the driver ORs flag bits in.
 * Units truncate (10 mA / 20 mV / 50 mA), so a sink never asks for more than
 * the caller said. */
#define PD_RDO_NO_USB_SUSPEND   (1ul << 24)
#define PD_RDO_USB_COMM_CAPABLE (1ul << 25)

uint32_t pd_request_fixed(uint8_t position, uint16_t operating_ma,
                          uint16_t max_operating_ma);
uint32_t pd_request_pps(uint8_t position, uint16_t out_mv,
                        uint16_t operating_ma);

/* The whole decision for one profile: bounds-check, pick the current (the
 * profile's max when want_ma is 0), build the right RDO kind. 0 means "this
 * profile cannot be requested" (bad index, battery/variable/unknown, or a
 * voltage outside a PPS range). */
uint32_t pd_request_for(const pd_caps_t *caps, int index,
                        uint16_t want_mv, uint16_t want_ma);

/* "Fixed" / "PPS" / "Battery" / "Variable" / "?" - for listings. */
const char *pd_supply_name(uint8_t kind);

#ifdef __cplusplus
}
#endif

#endif /* PD_FRAMES_H */
