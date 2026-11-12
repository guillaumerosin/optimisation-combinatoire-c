#include "schedule.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int decode_work_init(DecodeWork *work, const Instance *instance) {
    const size_t jobs = (size_t)instance->jobs;
    const size_t machines = (size_t)instance->machines;
    work->next = malloc(jobs * sizeof(int));
    work->job_ready = malloc(jobs * sizeof(int));
    work->machine_ready = malloc(machines * sizeof(int));
    work->earliest = malloc(jobs * sizeof(int));
    work->machine_earliest = malloc(machines * sizeof(int));
    work->current_machine = malloc(jobs * sizeof(int));
    if (!work->next || !work->job_ready || !work->machine_ready ||
        !work->earliest || !work->machine_earliest || !work->current_machine) {
        decode_work_free(work);
        return -1;
    }
    return 0;
}

void decode_work_free(DecodeWork *work) {
    free(work->next);
    free(work->job_ready);
    free(work->machine_ready);
    free(work->earliest);
    free(work->machine_earliest);
    free(work->current_machine);
    memset(work, 0, sizeof *work);
}

int schedule_init(Schedule *schedule, const Instance *instance) {
    schedule->makespan = 0;
    schedule->jobs = instance->jobs;
    schedule->machines = instance->machines;
    schedule->ops = calloc((size_t)instance->jobs * (size_t)instance->machines,
                           sizeof *schedule->ops);
    return schedule->ops ? 0 : -1;
}

void schedule_free(Schedule *schedule) {
    free(schedule->ops);
    schedule->ops = NULL;
}

int decode_semi_active(const Instance *instance, const int *sequence,
                       DecodeWork *work, int *starts) {
    const int jobs = instance->jobs;
    const int machines = instance->machines;
    // next[job] indique quelle operation de la recette doit etre planifiee.
    // Les deux tableaux ready memorisent les contraintes deja rencontrees.
    int *next = work->next;
    int *job_ready = work->job_ready;
    int *machine_ready = work->machine_ready;
    memset(next, 0, (size_t)jobs * sizeof(int));
    memset(job_ready, 0, (size_t)jobs * sizeof(int));
    memset(machine_ready, 0, (size_t)machines * sizeof(int));
    int makespan = 0;
    for (int step = 0; step < jobs * machines; ++step) {
        const int job = sequence[step];
        if (job < 0 || job >= jobs || next[job] >= machines) return -1;
        const int operation = next[job]++;
        const Operation *task = instance_op(instance, job, operation);
        // Une operation attend a la fois la fin du job precedent et la
        // liberation de sa machine.
        const int start = job_ready[job] > machine_ready[task->machine]
            ? job_ready[job] : machine_ready[task->machine];
        const int end = start + task->duration;
        if (starts) starts[job * machines + operation] = start;
        job_ready[job] = end;
        machine_ready[task->machine] = end;
        if (end > makespan) makespan = end;
    }
    return makespan;
}

int decode_active(const Instance *instance, const double *priorities,
                  double delta, DecodeWork *work, int *starts) {
    const int jobs = instance->jobs;
    const int machines = instance->machines;
    if (delta < 0.0) delta = 0.0;
    if (delta > 1.0) delta = 1.0;
    int *next = work->next;
    int *job_ready = work->job_ready;
    int *machine_ready = work->machine_ready;
    int *earliest = work->earliest;
    int *machine_earliest = work->machine_earliest;
    int *current_machine = work->current_machine;
    for (int job = 0; job < jobs; ++job) {
        next[job] = 0;
        job_ready[job] = 0;
        earliest[job] = 0;
        current_machine[job] = instance_op(instance, job, 0)->machine;
    }
    memset(machine_ready, 0, (size_t)machines * sizeof(int));
    int makespan = 0;
    for (int step = 0; step < jobs * machines; ++step) {
        // phi : plus petite date de fin parmi les operations disponibles.
        // Sa machine est celle ou se joue le prochain conflit. On note au
        // passage le plus petit debut possible sur chaque machine (sigma).
        for (int machine = 0; machine < machines; ++machine) {
            machine_earliest[machine] = INT_MAX;
        }
        int phi = INT_MAX;
        int conflict_machine = -1;
        for (int job = 0; job < jobs; ++job) {
            const int machine = current_machine[job];
            if (machine < 0) continue;
            const int begin = job_ready[job] > machine_ready[machine]
                ? job_ready[job] : machine_ready[machine];
            earliest[job] = begin;
            if (begin < machine_earliest[machine]) {
                machine_earliest[machine] = begin;
            }
            const int end =
                begin + instance_op(instance, job, next[job])->duration;
            if (end < phi) {
                phi = end;
                conflict_machine = machine;
            }
        }
        // delta regle la fenetre [sigma, sigma + delta (phi - sigma)] des
        // candidats ; parmi eux, la particule decide : cle la plus petite.
        const int sigma = machine_earliest[conflict_machine];
        const double limit = sigma + delta * (phi - sigma);
        int chosen = -1;
        double chosen_priority = 0.0;
        for (int job = 0; job < jobs; ++job) {
            if (current_machine[job] != conflict_machine ||
                earliest[job] > limit) {
                continue;
            }
            const double priority = priorities[job * machines + next[job]];
            if (chosen < 0 || priority < chosen_priority) {
                chosen = job;
                chosen_priority = priority;
            }
        }
        const int operation = next[chosen]++;
        const int end = earliest[chosen] +
                        instance_op(instance, chosen, operation)->duration;
        if (starts) starts[chosen * machines + operation] = earliest[chosen];
        job_ready[chosen] = end;
        machine_ready[conflict_machine] = end;
        current_machine[chosen] = next[chosen] < machines
            ? instance_op(instance, chosen, next[chosen])->machine : -1;
        if (end > makespan) makespan = end;
    }
    return makespan;
}

void schedule_from_starts(const Instance *instance, const int *starts,
                          Schedule *out) {
    const int machines = instance->machines;
    out->makespan = 0;
    for (int job = 0; job < instance->jobs; ++job) {
        for (int operation = 0; operation < machines; ++operation) {
            const Operation *task = instance_op(instance, job, operation);
            const int start = starts[job * machines + operation];
            ScheduledOperation *value = &out->ops[job * machines + operation];
            value->machine = task->machine;
            value->start = start;
            value->end = start + task->duration;
            if (value->end > out->makespan) out->makespan = value->end;
        }
    }
}

int schedule_write_json(const Schedule *schedule, const Instance *instance,
                        const char *path) {
    FILE *output = fopen(path, "w");
    if (!output) return -1;
    // Le JSON est volontairement ecrit a la main pour ne dependre d'aucune
    // bibliotheque externe.
    fprintf(output, "{\n  \"makespan\": %d,\n  \"jobs_schedule\": {\n",
            schedule->makespan);
    for (int job = 0; job < instance->jobs; ++job) {
        fprintf(output, "    \"Job_%d\": [", job);
        for (int operation = 0; operation < instance->machines; ++operation) {
            const ScheduledOperation *value =
                &schedule->ops[job * instance->machines + operation];
            if (operation != 0) fputs(", ", output);
            fprintf(output,
                    "{\"machine\": %d, \"start\": %d, \"end\": %d}",
                    value->machine, value->start, value->end);
        }
        fputs(job + 1 == instance->jobs ? "]\n" : "],\n", output);
    }
    fputs("  }\n}\n", output);
    return fclose(output) == 0 ? 0 : -1;
}
