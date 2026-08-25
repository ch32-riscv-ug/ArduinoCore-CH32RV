/* USB PD frame logic. See pd_frames.h for why this file is hardware-free. */
#include "pd_frames.h"

/* ---- header --------------------------------------------------------------
 * B15 extended | B14..12 count | B11..9 message id | B8 power role |
 * B7..6 spec revision | B5 data role | B4..0 type.
 * A sink that is also the UFP keeps both role bits 0, which is the only
 * header this library ever builds. */

uint16_t pd_header(uint8_t type, uint8_t count, uint8_t message_id,
                   uint8_t rev)
{
    return (uint16_t)((type & 0x1Fu)
                      | ((uint16_t)(rev & 0x3u) << 6)
                      | ((uint16_t)(message_id & 0x7u) << 9)
                      | ((uint16_t)(count & 0x7u) << 12));
}

uint8_t pd_header_type(uint16_t header)  { return header & 0x1Fu; }
uint8_t pd_header_count(uint16_t header) { return (header >> 12) & 0x7u; }
uint8_t pd_header_id(uint16_t header)    { return (header >> 9) & 0x7u; }
uint8_t pd_header_rev(uint16_t header)   { return (header >> 6) & 0x3u; }
int     pd_header_extended(uint16_t header) { return (header >> 15) & 1u; }

/* ---- Source_Capabilities ------------------------------------------------- */

static void parse_one(uint32_t raw, pd_pdo_t *out)
{
    out->raw = raw;
    out->min_mv = out->max_mv = 0;
    out->max_ma = 0;
    out->max_mw = 0;

    switch (raw >> 30) {
    case 0u:                                    /* fixed supply */
        out->kind = PD_SUPPLY_FIXED;
        /* B19..10 voltage in 50 mV, B9..0 max current in 10 mA */
        out->min_mv = out->max_mv = (uint16_t)(((raw >> 10) & 0x3FFu) * 50u);
        out->max_ma = (uint16_t)((raw & 0x3FFu) * 10u);
        break;
    case 1u:                                    /* battery */
        out->kind = PD_SUPPLY_BATTERY;
        /* B29..20 max voltage, B19..10 min voltage (50 mV),
         * B9..0 max allowable power in 250 mW */
        out->max_mv = (uint16_t)(((raw >> 20) & 0x3FFu) * 50u);
        out->min_mv = (uint16_t)(((raw >> 10) & 0x3FFu) * 50u);
        out->max_mw = (raw & 0x3FFu) * 250u;
        break;
    case 2u:                                    /* variable (unregulated) */
        out->kind = PD_SUPPLY_VARIABLE;
        /* Same voltage fields as battery; B9..0 max current in 10 mA */
        out->max_mv = (uint16_t)(((raw >> 20) & 0x3FFu) * 50u);
        out->min_mv = (uint16_t)(((raw >> 10) & 0x3FFu) * 50u);
        out->max_ma = (uint16_t)((raw & 0x3FFu) * 10u);
        break;
    default:                                    /* augmented (APDO) */
        if (((raw >> 28) & 0x3u) != 0u) {
            /* EPR AVS, or something newer. Listed, never requested. */
            out->kind = PD_SUPPLY_UNKNOWN;
            break;
        }
        out->kind = PD_SUPPLY_PPS;
        /* B24..17 max voltage, B15..8 min voltage (100 mV),
         * B6..0 max current in 50 mA */
        out->max_mv = (uint16_t)(((raw >> 17) & 0xFFu) * 100u);
        out->min_mv = (uint16_t)(((raw >> 8) & 0xFFu) * 100u);
        out->max_ma = (uint16_t)((raw & 0x7Fu) * 50u);
        break;
    }
}

void pd_parse_source_caps(const uint32_t *pdo, uint8_t count, pd_caps_t *out)
{
    out->count = count > PD_PDO_MAX ? PD_PDO_MAX : count;
    for (uint8_t i = 0; i < out->count; i++) {
        parse_one(pdo[i], &out->pdo[i]);
    }
    /* The spec makes PDO 1 the 5 V fixed supply, and puts the source's
     * character bits on it. Read them only from where they are defined to
     * be, so a malformed table cannot invent capabilities. */
    out->usb_comm = out->unconstrained = out->dual_role_power = 0;
    if (out->count && out->pdo[0].kind == PD_SUPPLY_FIXED) {
        out->usb_comm        = (pdo[0] >> 26) & 1u;
        out->unconstrained   = (pdo[0] >> 27) & 1u;
        out->dual_role_power = (pdo[0] >> 29) & 1u;
    }
}

int pd_pick(const pd_caps_t *caps, uint16_t want_mv, uint16_t want_ma)
{
    /* Fixed first: an exact level needs no keepalive. */
    for (uint8_t i = 0; i < caps->count; i++) {
        const pd_pdo_t *p = &caps->pdo[i];
        if (p->kind == PD_SUPPLY_FIXED && p->max_mv == want_mv
                && (want_ma == 0u || p->max_ma >= want_ma)) {
            return i;
        }
    }
    /* Then any PPS range holding the voltage; most current wins, first wins
     * a tie. Deterministic, and the headroom is why: on a charger offering
     * 3.3-11 V at 5 A and 3.3-16 V at 3 A, a 9 V request takes the 5 A one. */
    int best = -1;
    for (uint8_t i = 0; i < caps->count; i++) {
        const pd_pdo_t *p = &caps->pdo[i];
        if (p->kind == PD_SUPPLY_PPS
                && p->min_mv <= want_mv && want_mv <= p->max_mv
                && (want_ma == 0u || p->max_ma >= want_ma)
                && (best < 0 || caps->pdo[best].max_ma < p->max_ma)) {
            best = i;
        }
    }
    return best;
}

/* ---- request builders ----------------------------------------------------
 * Fixed/variable RDO: B31..28 position | B19..10 operating current |
 * B9..0 maximum operating current, both in 10 mA.
 * PPS RDO: B31..28 position | B20..9 output voltage in 20 mV |
 * B6..0 operating current in 50 mA. */

uint32_t pd_request_fixed(uint8_t position, uint16_t operating_ma,
                          uint16_t max_operating_ma)
{
    return ((uint32_t)(position & 0xFu) << 28)
           | (((uint32_t)operating_ma / 10u & 0x3FFu) << 10)
           | ((uint32_t)max_operating_ma / 10u & 0x3FFu);
}

uint32_t pd_request_pps(uint8_t position, uint16_t out_mv,
                        uint16_t operating_ma)
{
    return ((uint32_t)(position & 0xFu) << 28)
           | (((uint32_t)out_mv / 20u & 0xFFFu) << 9)
           | ((uint32_t)operating_ma / 50u & 0x7Fu);
}

uint32_t pd_request_for(const pd_caps_t *caps, int index,
                        uint16_t want_mv, uint16_t want_ma)
{
    if (index < 0 || index >= caps->count) {
        return 0;
    }
    const pd_pdo_t *p = &caps->pdo[index];
    /* Never more than the profile offers: a Request above the advertised
     * current is a spec violation the source answers with Reject. */
    uint16_t ma = (want_ma == 0u || want_ma > p->max_ma) ? p->max_ma : want_ma;
    const uint8_t position = (uint8_t)(index + 1);

    switch (p->kind) {
    case PD_SUPPLY_FIXED:
        return pd_request_fixed(position, ma, ma);
    case PD_SUPPLY_PPS:
        if (want_mv < p->min_mv || want_mv > p->max_mv) {
            return 0;
        }
        return pd_request_pps(position, want_mv, ma);
    default:
        return 0;
    }
}

const char *pd_supply_name(uint8_t kind)
{
    switch (kind) {
    case PD_SUPPLY_FIXED:    return "Fixed";
    case PD_SUPPLY_PPS:      return "PPS";
    case PD_SUPPLY_BATTERY:  return "Battery";
    case PD_SUPPLY_VARIABLE: return "Variable";
    default:                 return "?";
    }
}
