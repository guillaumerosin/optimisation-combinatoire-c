#include "instance.h"
#include "platform.h"
#include "pso.h"
#include "schedule.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_double(const char *text, double *out) {
    char *end = NULL;
    errno = 0;
    const double value = strtod(text, &end);
    if (end == text || *end != '\0' || errno == ERANGE) return -1;
    *out = value;
    return 0;
}

static int parse_int(const char *text, int *out) {
    char *end = NULL;
    errno = 0;
    const long value = strtol(text, &end, 10);
    if (end == text || *end != '\0' || errno == ERANGE ||
        value < INT_MIN || value > INT_MAX) {
        return -1;
    }
    *out = (int)value;
    return 0;
}

static int parse_u64(const char *text, uint64_t *out) {
    char *end = NULL;
    errno = 0;
    const unsigned long long value = strtoull(text, &end, 10);
    if (end == text || *end != '\0' || errno == ERANGE) return -1;
    *out = (uint64_t)value;
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fputs("Usage: jsp_pso <instance> [--time S] [--seed N] "
              "[--out DIR] [--threads N] [--decoder semi|gt] "
              "[--delta D] [--particles N] [--topology ring|global|cl|mixed] "
              "[--neighbors K] [--restart N] [--migrate N] [--vmax V] "
              "[--no-decreasing-inertia] [--target M] [--log FILE]\n",
              stderr);
        return 2;
    }
    // Les options restent simples pour pouvoir etre expliquees et
    // modifiees facilement pendant les essais du concours.
    SolverOptions options;
    solver_options_default(&options);
    char error[512] = "";
    const char *input = argv[1];
    for (int index = 2; index < argc; ++index) {
        const char *argument = argv[index];
        if (strcmp(argument, "--no-decreasing-inertia") == 0) {
            options.decreasing_inertia = false;
            continue;
        }
        if (index + 1 >= argc) {
            snprintf(error, sizeof error, "Option sans valeur: %s", argument);
            goto fail;
        }
        const char *value = argv[++index];
        int bad = 0;
        if (strcmp(argument, "--time") == 0) {
            bad = parse_double(value, &options.seconds);
            if (!bad && options.seconds < 0.0) {
                snprintf(error, sizeof error,
                         "--time doit etre positif ou nul");
                goto fail;
            }
        } else if (strcmp(argument, "--target") == 0) {
            bad = parse_int(value, &options.target_makespan);
        } else if (strcmp(argument, "--seed") == 0) {
            bad = parse_u64(value, &options.seed);
        } else if (strcmp(argument, "--out") == 0) {
            options.output_directory = value;
        } else if (strcmp(argument, "--threads") == 0) {
            bad = parse_int(value, &options.threads);
        } else if (strcmp(argument, "--particles") == 0) {
            bad = parse_int(value, &options.particles);
        } else if (strcmp(argument, "--decoder") == 0) {
            if (strcmp(value, "semi") == 0) {
                options.decoder = DECODER_SEMI;
            } else if (strcmp(value, "gt") == 0) {
                options.decoder = DECODER_GT;
            } else {
                snprintf(error, sizeof error, "Decodeur inconnu");
                goto fail;
            }
        } else if (strcmp(argument, "--delta") == 0) {
            bad = parse_double(value, &options.delta);
        } else if (strcmp(argument, "--topology") == 0) {
            if (strcmp(value, "ring") == 0) {
                options.topology = TOPOLOGY_RING;
            } else if (strcmp(value, "global") == 0) {
                options.topology = TOPOLOGY_GLOBAL;
            } else if (strcmp(value, "cl") == 0) {
                options.topology = TOPOLOGY_CL;
            } else if (strcmp(value, "mixed") == 0) {
                options.topology = TOPOLOGY_MIXED;
            } else {
                snprintf(error, sizeof error, "Topologie inconnue");
                goto fail;
            }
        } else if (strcmp(argument, "--neighbors") == 0) {
            bad = parse_int(value, &options.neighbors);
        } else if (strcmp(argument, "--restart") == 0) {
            bad = parse_int(value, &options.restart);
        } else if (strcmp(argument, "--migrate") == 0) {
            bad = parse_int(value, &options.migrate);
        } else if (strcmp(argument, "--vmax") == 0) {
            bad = parse_double(value, &options.max_velocity);
        } else if (strcmp(argument, "--log") == 0) {
            options.log_path = value;
        } else {
            snprintf(error, sizeof error, "Option inconnue: %s", argument);
            goto fail;
        }
        if (bad) {
            snprintf(error, sizeof error, "Valeur invalide pour %s: %s",
                     argument, value);
            goto fail;
        }
    }
    if (options.threads < 0 || options.particles <= 0 ||
        options.neighbors < 0 || options.restart < 0 ||
        options.migrate < 0 || options.max_velocity <= 0.0) {
        snprintf(error, sizeof error,
                 "Les parametres numeriques doivent etre positifs");
        goto fail;
    }

    Instance instance;
    if (instance_read(input, &instance, error, sizeof error) != 0) goto fail;
    Schedule schedule;
    if (schedule_init(&schedule, &instance) != 0) {
        instance_free(&instance);
        snprintf(error, sizeof error, "Memoire insuffisante");
        goto fail;
    }
    const double started = monotonic_seconds();
    const int status =
        solve(&instance, &options, &schedule, error, sizeof error);
    const double elapsed = monotonic_seconds() - started;
    if (status == 0) {
        printf("makespan=%d temps_secondes=%g\n", schedule.makespan, elapsed);
    }
    schedule_free(&schedule);
    instance_free(&instance);
    if (status != 0) goto fail;
    return 0;

fail:
    fprintf(stderr, "Erreur: %s\n", error);
    return 1;
}
