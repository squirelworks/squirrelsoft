#include "hb_health.h"

#include <stdio.h>
#include <string.h>

void hb_health_report(const HbProfile *p, HbHealthReport *out) {
    memset(out, 0, sizeof *out);
    if (!p) {
        snprintf(out->summary, sizeof out->summary, "no profile");
        return;
    }
    int i;
    for (i = 0; i < p->nch; i++) {
        const HbChannel *c = &p->ch[i];
        if (c->kind == HB_CH_ACTUATOR) out->n_actuator++;
        else if (c->kind == HB_CH_INCEPTOR) out->n_inceptor++;
        else if (c->kind == HB_CH_BUS) out->n_bus++;
        switch (c->health) {
        case HB_HEALTH_OK:
            out->n_ok++;
            break;
        case HB_HEALTH_DEGRADED:
            out->n_degraded++;
            break;
        case HB_HEALTH_FAULT:
            out->n_fault++;
            break;
        default:
            out->n_offline++;
            break;
        }
    }
    out->estop_ok = !p->estop_latched;
    out->safeing_safe = !p->safeing_armed;
    out->overall_ok = p->connected && out->estop_ok && out->n_fault == 0 && out->n_offline == 0;
    snprintf(out->summary, sizeof out->summary,
             "%s aircraft=%s transport=%s connected=%d channels=%d "
             "ok=%d degraded=%d fault=%d offline=%d estop=%s safeing=%s",
             out->overall_ok ? "PASS" : "FAIL", p->aircraft_id, p->transport, p->connected, p->nch,
             out->n_ok, out->n_degraded, out->n_fault, out->n_offline,
             out->estop_ok ? "clear" : "latched", out->safeing_safe ? "safe" : "armed_display");
}

int hb_health_format(const HbProfile *p, const HbHealthReport *r, char *out, int out_cap) {
    if (!p || !r || !out || out_cap < 64) return -1;
    int used = 0;
    int w = snprintf(out, out_cap, "%s\nkind            id                        health\n",
                     r->summary);
    if (w < 0 || w >= out_cap) return -1;
    used = w;
    int i;
    for (i = 0; i < p->nch; i++) {
        const HbChannel *c = &p->ch[i];
        w = snprintf(out + used, out_cap - used, "%-15s %-25s %s\n", hb_channel_kind_name(c->kind),
                     c->id, hb_health_name(c->health));
        if (w < 0 || used + w >= out_cap) return -1;
        used += w;
    }
    return used;
}
