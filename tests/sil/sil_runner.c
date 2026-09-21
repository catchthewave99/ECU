/*
 * sil_runner.c - Software-in-the-loop harness for the uno_baseline ECU firmware.
 *
 * Runs the real AVR machine code in simavr (instruction-accurate ATmega328P at
 * 16 MHz), stimulates the analog channels with fixed voltages, emulates the two
 * crank-signal jumper wires (D13->D2, D12->D3) and measures the injector output
 * on D4 in microseconds. This is the SIL layer of the test bench: it exercises
 * exactly the binary that gets flashed, so measured pulse widths and RPM can be
 * compared one-for-one against the HIL measurements taken with the NI hardware.
 *
 * Build: make -C tests/sil
 * Usage: sil_runner <firmware.elf> --rpm 2000 --cht-mv 2500 --lambda-mv 2295
 *                   --ms 4000 [--quiet]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <simavr/sim_avr.h>
#include <simavr/sim_elf.h>
#include <simavr/avr_ioport.h>
#include <simavr/avr_adc.h>
#include <simavr/avr_uart.h>

static avr_t *avr;

static int adc_mv[8] = { 3400, 2500, 2500, 2295, 0, 512, 0, 0 };

static avr_irq_t *pd_irq;   /* PORTD pin irqs: crank inputs D2/D3 */

/* injector edge measurement on D4 (PORTD bit 4) */
static uint64_t inj_rise_cycle;
static uint64_t inj_last_rise;
static double inj_widths_us[4096];
static int inj_count;

/* injection phasing: injector rise to the following TDC edge */
static double adv_us[4096];
static int adv_count;

/* overspeed safety output D9 (PORTB bit 1) */
static int safety_asserts;
static int safety_level;

/* TDC edge timing as seen by the harness, to check the crank stimulus itself */
static uint64_t tdc_last_cycle;
static double tdc_periods_us[4096];
static int tdc_count;

static int quiet;

static double cycles_to_us(uint64_t cycles)
{
    return (double)cycles / 16.0; /* 16 MHz */
}

static void adc_hook(struct avr_irq_t *irq, uint32_t value, void *param)
{
    union { uint32_t u32; avr_adc_mux_t mux; } v;
    v.u32 = value;
    unsigned src = v.mux.src;
    if (src >= 8)
        return;
    avr_raise_irq(avr_io_getirq(avr, AVR_IOCTL_ADC_GETIRQ, ADC_IRQ_ADC0 + src),
                  adc_mv[src]);
}

static void injector_hook(struct avr_irq_t *irq, uint32_t value, void *param)
{
    if (value) {
        inj_rise_cycle = avr->cycle;
        inj_last_rise = avr->cycle;
    } else if (inj_rise_cycle) {
        double us = cycles_to_us(avr->cycle - inj_rise_cycle);
        if (inj_count < (int)(sizeof(inj_widths_us) / sizeof(inj_widths_us[0])))
            inj_widths_us[inj_count++] = us;
        inj_rise_cycle = 0;
    }
}

/* Jumper D13 (PORTB5) -> D2 (PORTD2): TDC */
static void jumper_tdc_hook(struct avr_irq_t *irq, uint32_t value, void *param)
{
    avr_raise_irq(pd_irq + IOPORT_IRQ_PIN2, value);
    if (!value) {
        if (inj_last_rise &&
            adv_count < (int)(sizeof(adv_us) / sizeof(adv_us[0]))) {
            adv_us[adv_count++] = cycles_to_us(avr->cycle - inj_last_rise);
            inj_last_rise = 0;
        }
        if (tdc_last_cycle &&
            tdc_count < (int)(sizeof(tdc_periods_us) / sizeof(tdc_periods_us[0])))
            tdc_periods_us[tdc_count++] = cycles_to_us(avr->cycle - tdc_last_cycle);
        tdc_last_cycle = avr->cycle;
    }
}

/* Jumper D12 (PORTB4) -> D3 (PORTD3): pre-TDC */
static void jumper_pretdc_hook(struct avr_irq_t *irq, uint32_t value, void *param)
{
    avr_raise_irq(pd_irq + IOPORT_IRQ_PIN3, value);
}

static void safety_hook(struct avr_irq_t *irq, uint32_t value, void *param)
{
    safety_level = value ? 1 : 0;
    if (value)
        safety_asserts++;
}

static void uart_out_hook(struct avr_irq_t *irq, uint32_t value, void *param)
{
    if (!quiet)
        fputc(value & 0xff, stdout);
}

static void uart_send(const char *s)
{
    avr_irq_t *in = avr_io_getirq(avr, AVR_IOCTL_UART_GETIRQ('0'), UART_IRQ_INPUT);
    while (*s)
        avr_raise_irq(in, *s++);
}

static int cmp_double(const void *a, const void *b)
{
    double da = *(const double *)a, db = *(const double *)b;
    return (da > db) - (da < db);
}

static void stats(const char *label, double *v, int n, int skip)
{
    if (n <= skip) {
        printf("%-16s no samples\n", label);
        return;
    }
    double *w = v + skip;
    int m = n - skip;
    double sum = 0;
    for (int i = 0; i < m; i++)
        sum += w[i];
    double *sorted = malloc(m * sizeof(double));
    memcpy(sorted, w, m * sizeof(double));
    qsort(sorted, m, sizeof(double), cmp_double);
    printf("%-16s n=%-5d min=%.2f mean=%.2f median=%.2f max=%.2f\n",
           label, m, sorted[0], sum / m, sorted[m / 2], sorted[m - 1]);
    free(sorted);
}

int main(int argc, char **argv)
{
    const char *elf_path = NULL;
    int rpm = 2000;
    int run_ms = 4000;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--rpm") && i + 1 < argc) rpm = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--ms") && i + 1 < argc) run_ms = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--mat-mv") && i + 1 < argc) adc_mv[0] = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--cht-mv") && i + 1 < argc) adc_mv[1] = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--map-mv") && i + 1 < argc) adc_mv[2] = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--lambda-mv") && i + 1 < argc) adc_mv[3] = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--tps-mv") && i + 1 < argc) adc_mv[4] = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--pot-mv") && i + 1 < argc) adc_mv[5] = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--quiet")) quiet = 1;
        else elf_path = argv[i];
    }

    if (!elf_path) {
        fprintf(stderr, "usage: sil_runner <firmware.elf> [--rpm n] [--ms n] [--cht-mv n] ...\n");
        return 2;
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

    /* Uno R3 rails: simavr defaults to a 3.3 V reference, which would clip the
       5 V sensor range and silently shift every engineering-unit conversion. */
    avr->vcc = 5000;
    avr->avcc = 5000;
    avr->aref = 5000;

    pd_irq = avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('D'), 0);

    avr_irq_register_notify(avr_io_getirq(avr, AVR_IOCTL_ADC_GETIRQ, ADC_IRQ_OUT_TRIGGER),
                            adc_hook, NULL);
    avr_irq_register_notify(pd_irq + IOPORT_IRQ_PIN4, injector_hook, NULL);
    avr_irq_register_notify(avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('B'), IOPORT_IRQ_PIN5),
                            jumper_tdc_hook, NULL);
    avr_irq_register_notify(avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('B'), IOPORT_IRQ_PIN4),
                            jumper_pretdc_hook, NULL);
    avr_irq_register_notify(avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('B'), IOPORT_IRQ_PIN1),
                            safety_hook, NULL);
    avr_irq_register_notify(avr_io_getirq(avr, AVR_IOCTL_UART_GETIRQ('0'), UART_IRQ_OUTPUT),
                            uart_out_hook, NULL);

    /* let setup() finish (it contains a 300 ms prime pulse) before cranking */
    uint64_t cycles_settle = 16000000ULL * 600 / 1000;
    while (avr->cycle < cycles_settle && avr_run(avr) < cpu_Done)
        ;

    inj_count = 0;          /* discard the prime pulse */
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "R%d\n", rpm);
    uart_send(cmd);

    uint64_t cycles_end = cycles_settle + 16000000ULL * run_ms / 1000;
    while (avr->cycle < cycles_end && avr_run(avr) < cpu_Done)
        ;

    printf("\n=== SIL results: rpm_sp=%d cht_mv=%d lambda_mv=%d pot_mv=%d run_ms=%d ===\n",
           rpm, adc_mv[1], adc_mv[3], adc_mv[5], run_ms);
    stats("tdc_period_us", tdc_periods_us, tdc_count, 1);
    stats("inj_pulse_us", inj_widths_us, inj_count, 2);
    stats("inj_to_tdc_us", adv_us, adv_count, 2);
    if (tdc_count > 2) {
        double sum = 0;
        for (int i = 1; i < tdc_count; i++) sum += tdc_periods_us[i];
        double mean = sum / (tdc_count - 1);
        printf("%-16s %.1f rpm (setpoint %d, error %+.2f %%)\n", "crank_speed",
               60e6 / mean, rpm, 100.0 * (60e6 / mean - rpm) / rpm);
    }
    printf("%-16s %d\n", "inj_pulses", inj_count);
    printf("%-16s asserts=%d level=%d\n", "safety_pin_d9", safety_asserts, safety_level);
    return 0;
}
