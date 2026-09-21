/*
 * sil_runner.c - Software-in-the-loop harness for the Uno ECU firmware.
 *
 * Runs the real AVR machine code in simavr (instruction-accurate ATmega328P at
 * 16 MHz), so what is measured here is the same binary that gets flashed to the
 * board. The harness plays the part of the NI rig: it drives the switch inputs
 * and the temperature sensor, and it watches the lamp, fan and heartbeat
 * outputs with microsecond resolution.
 *
 * It is deliberately generic: the same runner drives functions/thermal,
 * functions/body, functions/hazard and consolidated, because they all use the
 * pin map in docs/03-wiring-chart.md.
 *
 * Build: make -C tests/sil
 * Usage: sil_runner <firmware.elf> --ms 3000 [--temp-mv 800]
 *                   [--at <ms> <signal>=<value>] ... [--quiet]
 *
 *   signals: hazard, head, turnl, turnr  (value 0 or 1)
 *            d2, d7, d4, d5              the same four lines, named by pin, for
 *                                        sketches that put a switch elsewhere
 *            temp                        (value in millivolts)
 *
 * --taps N --tap-ms M presses the hazard switch N times, M ms apart, starting
 * 1 s in, and reports the response time of every press. Pressing repeatedly
 * matters: a slow job holds the lamps up only for presses that land while it is
 * busy, so a single press can pass a firmware that a driver would fail.
 *
 * Example - press the hazard switch one second in and see how long the lamps
 * take to light:
 *   sil_runner build/consolidated/consolidated.ino.elf --ms 3000 \
 *              --temp-mv 800 --at 1000 hazard=1
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <simavr/sim_avr.h>
#include <simavr/sim_elf.h>
#include <simavr/avr_ioport.h>
#include <simavr/avr_adc.h>
#include <simavr/avr_uart.h>

#define MAX_EVENTS 64
#define MAX_EDGES  8192

static avr_t *avr;
static int quiet;

/* ---- the sensor -------------------------------------------------------- */
static int temp_mv = 800;              /* A0: 10 mV per degree C */

/* ---- switch inputs, as the rig drives them ---------------------------- */
/* Arduino pin -> AVR port: D0-D7 are PORTD bits 0-7, D8-D13 are PORTB 0-5. */
struct in_pin {
    const char *name;
    const char *pin_name;
    char port;
    int bit;
    int level;
};

static struct in_pin inputs[] = {
    { "hazard", "d2", 'D', 2, 0 },   /* hazard switch */
    { "turnl",  "d4", 'D', 4, 0 },   /* turn stalk left */
    { "turnr",  "d5", 'D', 5, 0 },   /* turn stalk right */
    { "head",   "d7", 'D', 7, 0 },   /* headlamp switch (D2 in the DEF-101 build) */
};
static const int n_inputs = sizeof(inputs) / sizeof(inputs[0]);

/* ---- outputs the harness measures ------------------------------------- */
struct out_pin {
    const char *name;
    char port;
    int bit;
    int level;
    int rises;
    int falls;
    int n_edges;
    double last_edge_ms;
    double max_gap_ms;     /* largest gap between two edges, however many */
    double edge_ms[MAX_EDGES];
};

static struct out_pin outputs[] = {
    { "lamp_left", 'B', 0 },   /* D8  */
    { "lamp_right", 'B', 1 },  /* D9  */
    { "headlamp", 'B', 2 },    /* D10 */
    { "fan", 'B', 3 },         /* D11 */
    { "overtemp", 'B', 4 },    /* D12 */
    { "heartbeat", 'B', 5 },   /* D13 */
};
static const int n_outputs = sizeof(outputs) / sizeof(outputs[0]);

/* ---- timed stimulus --------------------------------------------------- */
struct event {
    unsigned long at_ms;
    int input_index;   /* -1 means it is a temperature change */
    int value;
    int done;
};

static struct event events[MAX_EVENTS];
static int n_events;

/* ---- what we are timing ----------------------------------------------- */
/* Hazard response: switch closed -> first hazard lamp on, once per press. */
static double pending_press_ms = -1;
static double responses_ms[MAX_EVENTS];
static int n_responses;
static int n_presses;

static double now_ms(void)
{
    return (double)avr->cycle / 16000.0;   /* 16 MHz */
}

static avr_irq_t *port_irq(char port, int bit)
{
    return avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ(port), bit);
}

static void adc_hook(struct avr_irq_t *irq, uint32_t value, void *param)
{
    union { uint32_t u32; avr_adc_mux_t mux; } v;
    v.u32 = value;
    if (v.mux.src >= 8)
        return;
    /* Only A0 is wired on this bench; the rest read as 0 V. */
    int mv = (v.mux.src == 0) ? temp_mv : 0;
    avr_raise_irq(avr_io_getirq(avr, AVR_IOCTL_ADC_GETIRQ, ADC_IRQ_ADC0 + v.mux.src), mv);
}

static void out_hook(struct avr_irq_t *irq, uint32_t value, void *param)
{
    struct out_pin *p = param;
    int level = value ? 1 : 0;
    if (level == p->level)
        return;
    p->level = level;
    if (level) p->rises++; else p->falls++;
    double t = now_ms();
    if (p->n_edges && t - p->last_edge_ms > p->max_gap_ms)
        p->max_gap_ms = t - p->last_edge_ms;
    p->last_edge_ms = t;
    if (p->n_edges < MAX_EDGES)
        p->edge_ms[p->n_edges] = t;
    p->n_edges++;

    /* The safety measurement: hazard switch closed -> first lamp on. */
    if (level && pending_press_ms >= 0 &&
        (!strcmp(p->name, "lamp_left") || !strcmp(p->name, "lamp_right"))) {
        if (n_responses < MAX_EVENTS)
            responses_ms[n_responses++] = t - pending_press_ms;
        pending_press_ms = -1;
    }
}

static void uart_out_hook(struct avr_irq_t *irq, uint32_t value, void *param)
{
    if (!quiet)
        fputc(value & 0xff, stdout);
}

static int find_input(const char *name)
{
    for (int i = 0; i < n_inputs; i++)
        if (!strcmp(inputs[i].name, name) || !strcmp(inputs[i].pin_name, name))
            return i;
    return -1;
}

static void drive_inputs(void)
{
    for (int i = 0; i < n_inputs; i++)
        avr_raise_irq(port_irq(inputs[i].port, inputs[i].bit), inputs[i].level);
}

static void add_event(unsigned long at_ms, int input_index, int value)
{
    if (n_events >= MAX_EVENTS) {
        fprintf(stderr, "sil_runner: too many stimulus events\n");
        exit(2);
    }
    struct event *e = &events[n_events++];
    e->at_ms = at_ms;
    e->input_index = input_index;
    e->value = value;
}

static int cmp_double(const void *a, const void *b)
{
    double da = *(const double *)a, db = *(const double *)b;
    return (da > db) - (da < db);
}

/* Prints min/mean/median/max of the gaps between consecutive edges. */
static void value_stats(const char *label, const double *v, int n)
{
    if (n <= 0) {
        printf("%-18s no samples\n", label);
        return;
    }
    double *sorted = malloc(n * sizeof(double));
    double sum = 0;
    for (int i = 0; i < n; i++) {
        sorted[i] = v[i];
        sum += v[i];
    }
    qsort(sorted, n, sizeof(double), cmp_double);
    printf("%-18s n=%-4d min=%.3f mean=%.3f median=%.3f max=%.3f\n",
           label, n, sorted[0], sum / n, sorted[n / 2], sorted[n - 1]);
    free(sorted);
}

static void edge_gap_stats(const char *label, struct out_pin *p, int skip)
{
    int stored = p->n_edges < MAX_EDGES ? p->n_edges : MAX_EDGES;
    int n = stored - 1 - skip;
    if (n <= 0) {
        printf("%-18s no samples\n", label);
        return;
    }
    double *gaps = malloc(n * sizeof(double));
    double sum = 0;
    for (int i = 0; i < n; i++) {
        gaps[i] = p->edge_ms[skip + i + 1] - p->edge_ms[skip + i];
        sum += gaps[i];
    }
    qsort(gaps, n, sizeof(double), cmp_double);
    printf("%-18s n=%-4d min=%.3f mean=%.3f median=%.3f max=%.3f\n",
           label, n, gaps[0], sum / n, gaps[n / 2], gaps[n - 1]);
    free(gaps);
}

int main(int argc, char **argv)
{
    const char *elf_path = NULL;
    unsigned long run_ms = 3000;
    int taps = 0;
    unsigned long tap_ms = 500;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--ms") && i + 1 < argc) {
            run_ms = strtoul(argv[++i], NULL, 10);
        } else if (!strcmp(argv[i], "--temp-mv") && i + 1 < argc) {
            temp_mv = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--taps") && i + 1 < argc) {
            taps = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--tap-ms") && i + 1 < argc) {
            tap_ms = strtoul(argv[++i], NULL, 10);
        } else if (!strcmp(argv[i], "--quiet")) {
            quiet = 1;
        } else if (!strcmp(argv[i], "--at") && i + 2 < argc) {
            unsigned long at_ms = strtoul(argv[++i], NULL, 10);
            char *spec = argv[++i];
            char *eq = strchr(spec, '=');
            if (!eq) {
                fprintf(stderr, "sil_runner: --at wants <signal>=<value>, got %s\n", spec);
                return 2;
            }
            *eq = 0;
            int index = strcmp(spec, "temp") ? find_input(spec) : -1;
            if (strcmp(spec, "temp") && index < 0) {
                fprintf(stderr, "sil_runner: unknown signal %s\n", spec);
                return 2;
            }
            add_event(at_ms, index, atoi(eq + 1));
        } else {
            elf_path = argv[i];
        }
    }

    if (!elf_path) {
        fprintf(stderr, "usage: sil_runner <firmware.elf> [--ms n] [--temp-mv n]"
                        " [--at <ms> <signal>=<value>] [--quiet]\n");
        return 2;
    }

    /* Hazard taps: hold the switch for half the period, release for the rest. */
    int hazard_index = find_input("hazard");
    for (int i = 0; i < taps; i++) {
        add_event(1000 + i * tap_ms, hazard_index, 1);
        add_event(1000 + i * tap_ms + tap_ms / 2, hazard_index, 0);
    }

    elf_firmware_t f = {{0}};
    if (elf_read_firmware(elf_path, &f) < 0) {
        fprintf(stderr, "sil_runner: cannot read %s\n", elf_path);
        return 2;
    }
    strcpy(f.mmcu, "atmega328p");
    f.frequency = 16000000;

    avr = avr_make_mcu_by_name(f.mmcu);
    if (!avr) {
        fprintf(stderr, "sil_runner: unknown mcu %s\n", f.mmcu);
        return 2;
    }
    avr_init(avr);
    avr_load_firmware(avr, &f);

    /* Uno R3 rails. simavr defaults to 3.3 V, which would shift every
       temperature reading by a third. */
    avr->vcc = 5000;
    avr->avcc = 5000;
    avr->aref = 5000;

    avr_irq_register_notify(avr_io_getirq(avr, AVR_IOCTL_ADC_GETIRQ, ADC_IRQ_OUT_TRIGGER),
                            adc_hook, NULL);
    for (int i = 0; i < n_outputs; i++)
        avr_irq_register_notify(port_irq(outputs[i].port, outputs[i].bit),
                                out_hook, &outputs[i]);
    avr_irq_register_notify(avr_io_getirq(avr, AVR_IOCTL_UART_GETIRQ('0'), UART_IRQ_OUTPUT),
                            uart_out_hook, NULL);

    /* Step in 100 us slices: apply the stimulus, hold the switch lines, run. */
    const uint64_t slice_cycles = 1600;
    uint64_t end_cycle = 16000ULL * run_ms;
    int running = 1;

    while (running && avr->cycle < end_cycle) {
        double t = now_ms();
        for (int i = 0; i < n_events; i++) {
            struct event *e = &events[i];
            if (e->done || t < (double)e->at_ms)
                continue;
            e->done = 1;
            if (e->input_index < 0) {
                temp_mv = e->value;
            } else {
                inputs[e->input_index].level = e->value ? 1 : 0;
                if (e->input_index == find_input("hazard") && e->value) {
                    pending_press_ms = t;
                    n_presses++;
                }
            }
        }
        drive_inputs();

        uint64_t slice_end = avr->cycle + slice_cycles;
        while (avr->cycle < slice_end) {
            if (avr_run(avr) >= cpu_Done) {
                running = 0;
                break;
            }
        }
    }

    printf("\n=== SIL results: elf=%s run_ms=%lu temp_mv=%d ===\n",
           elf_path, run_ms, temp_mv);
    for (int i = 0; i < n_outputs; i++)
        printf("%-18s level=%d rises=%d falls=%d\n", outputs[i].name,
               outputs[i].level, outputs[i].rises, outputs[i].falls);

    if (n_presses) {
        /* A press that never lit a lamp is not a missing sample, it is a
           failure: score it far outside any limit rather than dropping it. */
        for (int i = n_responses; i < n_presses && i < MAX_EVENTS; i++)
            responses_ms[i] = 9999.0;
        value_stats("hazard_response_ms", responses_ms,
                    n_presses < MAX_EVENTS ? n_presses : MAX_EVENTS);
    }

    /* A flashing lamp gives one edge per half period. Skip the first edge: it
       is the switch press, not the flasher. */
    edge_gap_stats("flash_half_ms", &outputs[0], 1);
    /* The heartbeat flips once per pass of loop(), so the largest gap between
       heartbeat edges is the slowest pass of loop(). */
    printf("%-18s edges=%d max=%.3f\n", "loop_ms",
           outputs[5].n_edges, outputs[5].max_gap_ms);
    return 0;
}
