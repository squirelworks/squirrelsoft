/* SquirrelHILBench — Soft HIL bench UI for ISM fighter (Victor). Original code. */
#define _POSIX_C_SOURCE 200809L
#include "hb_health.h"
#include "hb_log.h"
#include "hb_profile.h"
#include "hb_stimulus.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(FILE *f) {
    fputs(
        "usage: squirrelhilbench <command> [options]\n"
        "\n"
        "  help\n"
        "  load [--profile FILE]              load mock bench profile + connect\n"
        "  health [--profile FILE]            channel health (actuators/inceptors/bus)\n"
        "  stimulus [--profile FILE] [--script FILE] [--csv OUT]\n"
        "                                     run canned stimulus; save Flight Lab replay CSV\n"
        "  demo                               load + health + stimulus against CI fixtures\n"
        "\n"
        "Soft operator UI for ISM fighter HIL benches. Physical harness / fixture notes\n"
        "are SoT in international-squirrel-machines (fighter-jet/). CI uses fixtures/\n"
        "until Victor publishes harness docs. Flight Lab (squirrelflightlab/) is Steve's\n"
        "separate aero lab — this app does not duplicate it.\n"
        "\n"
        "Weapons boundary: stores integration and display only. No guidance, seekers,\n"
        "propulsion, warheads, fuzing, arming codes, or weapon-protocol internals.\n",
        f);
}

static int read_file(const char *path, char **out) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }
    long n = ftell(f);
    if (n < 0 || n > 1 << 20) {
        fclose(f);
        return -1;
    }
    rewind(f);
    char *b = malloc((size_t)n + 1);
    if (!b) {
        fclose(f);
        return -1;
    }
    if (fread(b, 1, (size_t)n, f) != (size_t)n) {
        free(b);
        fclose(f);
        return -1;
    }
    b[n] = 0;
    fclose(f);
    *out = b;
    return 0;
}

static int write_text(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t n = strlen(text);
    if (fwrite(text, 1, n, f) != n) {
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

static const char *opt_val(int argc, char **argv, int *i) {
    if (*i + 1 >= argc) return NULL;
    (*i)++;
    return argv[*i];
}

static int load_connected(const char *path, HbProfile *p) {
    char *text = NULL, err[HB_MSG_LEN];
    if (read_file(path, &text) != 0) {
        fprintf(stderr, "squirrelhilbench: cannot read %s\n", path);
        return 1;
    }
    if (hb_profile_parse(p, text, err, sizeof err) != 0) {
        fprintf(stderr, "squirrelhilbench: %s\n", err);
        free(text);
        return 1;
    }
    free(text);
    if (hb_profile_connect_mock(p, err, sizeof err) != 0) {
        fprintf(stderr, "squirrelhilbench: %s\n", err);
        return 1;
    }
    return 0;
}

static int cmd_load(int argc, char **argv) {
    const char *path = "fixtures/ism-fighter-hil/bench-profile.ismhil";
    int i;
    for (i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--profile") == 0) {
            path = opt_val(argc, argv, &i);
            if (!path) return 2;
        } else return 2;
    }
    HbProfile p;
    if (load_connected(path, &p) != 0) return 1;
    printf("loaded aircraft=%s transport=%s endpoint=%s channels=%d connected=1\n", p.aircraft_id,
           p.transport, p.endpoint, p.nch);
    printf("estop=%s safeing=%s\n", p.estop_latched ? "latched" : "clear",
           p.safeing_armed ? "armed_display" : "safe");
    return 0;
}

static int cmd_health(int argc, char **argv) {
    const char *path = "fixtures/ism-fighter-hil/bench-profile.ismhil";
    int i;
    for (i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--profile") == 0) {
            path = opt_val(argc, argv, &i);
            if (!path) return 2;
        } else return 2;
    }
    HbProfile p;
    if (load_connected(path, &p) != 0) return 1;
    HbHealthReport r;
    hb_health_report(&p, &r);
    char buf[8192];
    if (hb_health_format(&p, &r, buf, sizeof buf) < 0) {
        fprintf(stderr, "squirrelhilbench: health format overflow\n");
        return 1;
    }
    fputs(buf, stdout);
    return r.overall_ok ? 0 : 1;
}

static int cmd_stimulus(int argc, char **argv) {
    const char *profile = "fixtures/ism-fighter-hil/bench-profile.ismhil";
    const char *script = "fixtures/ism-fighter-hil/stimulus-trim.stim";
    const char *csv = NULL;
    int i;
    for (i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--profile") == 0) {
            profile = opt_val(argc, argv, &i);
            if (!profile) return 2;
        } else if (strcmp(argv[i], "--script") == 0) {
            script = opt_val(argc, argv, &i);
            if (!script) return 2;
        } else if (strcmp(argv[i], "--csv") == 0) {
            csv = opt_val(argc, argv, &i);
            if (!csv) return 2;
        } else return 2;
    }
    HbProfile p;
    if (load_connected(profile, &p) != 0) return 1;
    char *text = NULL, err[HB_MSG_LEN];
    if (read_file(script, &text) != 0) {
        fprintf(stderr, "squirrelhilbench: cannot read %s\n", script);
        return 1;
    }
    HbStimulus stim;
    if (hb_stimulus_parse(&stim, text, err, sizeof err) != 0) {
        fprintf(stderr, "squirrelhilbench: %s\n", err);
        free(text);
        return 1;
    }
    free(text);
    HbStimResult res;
    if (hb_stimulus_run(&p, &stim, &res, err, sizeof err) != 0) {
        fprintf(stderr, "squirrelhilbench: %s\n", err);
        return 1;
    }
    puts(res.summary);
    char table[1 << 20];
    if (hb_log_format_replay_csv(&res, table, sizeof table) < 0) {
        fprintf(stderr, "squirrelhilbench: replay csv buffer full\n");
        return 1;
    }
    int lines = 0;
    const char *s = table;
    while (*s && lines < 6) {
        const char *e = strchr(s, '\n');
        if (!e) break;
        fwrite(s, 1, (size_t)(e - s + 1), stdout);
        s = e + 1;
        lines++;
    }
    if (*s) puts("...");
    if (!csv) csv = HB_DEFAULT_REPLAY_NAME;
    if (write_text(csv, table) != 0) {
        fprintf(stderr, "squirrelhilbench: cannot write %s\n", csv);
        return 1;
    }
    printf("wrote Flight Lab replay-compatible log %s (%d samples)\n", csv, res.nsamples);
    return res.ok ? 0 : 1;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage(stderr);
        return 2;
    }
    const char *cmd = argv[1];
    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "--help") == 0) {
        usage(stdout);
        return 0;
    }
    if (strcmp(cmd, "load") == 0) return cmd_load(argc - 2, argv + 2);
    if (strcmp(cmd, "health") == 0) return cmd_health(argc - 2, argv + 2);
    if (strcmp(cmd, "stimulus") == 0) return cmd_stimulus(argc - 2, argv + 2);
    if (strcmp(cmd, "demo") == 0) {
        int a = cmd_load(0, NULL);
        int b = cmd_health(0, NULL);
        char *args[] = {"--csv", "build/hil-stim-replay.csv"};
        int c = cmd_stimulus(2, args);
        if (a || b || c) return 1;
        puts("squirrelhilbench demo ok");
        return 0;
    }
    fprintf(stderr, "squirrelhilbench: unknown command '%s'\n", cmd);
    usage(stderr);
    return 2;
}
