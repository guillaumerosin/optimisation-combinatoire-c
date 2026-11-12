#include "instance.h"
#include "pso.h"
#include "schedule.h"

// En Release, CMake definit NDEBUG, ce qui desactive assert : les tests
// passeraient toujours. On le retire avant d'inclure <assert.h>.
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void write_file(const char *path, const char *content) {
    FILE *file = fopen(path, "wb");
    assert(file);
    fputs(content, file);
    fclose(file);
}

static int read_fails(const char *content) {
    write_file("bad_instance.txt", content);
    Instance instance;
    char error[256];
    if (instance_read("bad_instance.txt", &instance, error, sizeof error) != 0) {
        return 1;
    }
    instance_free(&instance);
    return 0;
}

static void test_read_instance_edge_cases(void) {
    // Fichier Windows avec une ligne vide : le nom ne garde pas le '\r'.
    write_file("crlf_instance.txt",
               "exemple\r\n3 3\r\n\r\n0 3 1 2 2 4\r\n"
               "1 4 2 3 0 2\r\n2 2 0 5 1 3\r\n");
    Instance crlf;
    char error[256];
    assert(instance_read("crlf_instance.txt", &crlf, error, sizeof error) == 0);
    assert(strcmp(crlf.name, "exemple") == 0);
    assert(crlf.jobs == 3 && crlf.machines == 3);
    assert(instance_op(&crlf, 1, 0)->machine == 1);
    assert(instance_op(&crlf, 1, 0)->duration == 4);
    assert(instance_op(&crlf, 2, 2)->machine == 1);
    assert(instance_op(&crlf, 2, 2)->duration == 3);
    instance_free(&crlf);

#define JOBS "1 4 2 3 0 2\n2 2 0 5 1 3\n"
    assert(read_fails("x\n3 3\n0 3 1 2\n" JOBS));          // trop court
    assert(read_fails("x\n3 3\n0 3 1 2 2 4 1 1\n" JOBS));  // trop long
    assert(read_fails("x\n3 3\n0 3 0 2 2 4\n" JOBS));      // machine 0 deux fois
    assert(read_fails("x\n3 3\n0 3 1 2 5 4\n" JOBS));      // machine 5 inexistante
    assert(read_fails("x\n3 3\n0 3 1 2 2 4\n1 4 2 3 0 2\n")); // job manquant
    assert(read_fails("x\n3 3 7\n0 3 1 2 2 4\n" JOBS));    // dimensions
    assert(read_fails("x\n3 3\n0 3 1 a 2 4\n" JOBS));      // pas un nombre
    assert(read_fails(""));                                 // vide
#undef JOBS
}

int main(void) {
    test_read_instance_edge_cases();
    write_file("test_instance.txt",
               "cours\n3 3\n"
               "0 3 1 2 2 4\n"
               "1 4 2 3 0 2\n"
               "2 2 0 5 1 3\n");
    Instance instance;
    char error[256];
    assert(instance_read("test_instance.txt", &instance, error,
                         sizeof error) == 0);
    assert(strcmp(instance.name, "cours") == 0);

    DecodeWork work;
    assert(decode_work_init(&work, &instance) == 0);
    Schedule schedule;
    assert(schedule_init(&schedule, &instance) == 0);
    const int sequence[] = {1, 0, 2, 2, 0, 1, 1, 2, 0};
    int starts[9];
    assert(decode_semi_active(&instance, sequence, &work, starts) == 11);
    schedule_from_starts(&instance, starts, &schedule);
    assert(schedule.makespan == 11);
    assert(schedule_write_json(&schedule, &instance, "test_result.json") == 0);
    // Une sequence ou un job apparait trop souvent est refusee.
    const int invalid[] = {0, 0, 0, 0, 1, 1, 1, 2, 2};
    assert(decode_semi_active(&instance, invalid, &work, NULL) == -1);
    decode_work_free(&work);

    // Le PSO seul doit trouver l'optimum (11) sur l'exemple du cours.
    SolverOptions options;
    solver_options_default(&options);
    options.seconds = 1.0;
    options.target_makespan = 11;
    options.output_directory = "pso_output";
    Schedule solved;
    assert(schedule_init(&solved, &instance) == 0);
    assert(solve(&instance, &options, &solved, error, sizeof error) == 0);
    assert(solved.makespan == 11);
    schedule_free(&solved);
    schedule_free(&schedule);
    instance_free(&instance);

    write_file("unnamed_instance.txt",
               "3 3\n"
               "0 3 1 2 2 4\n"
               "1 4 2 3 0 2\n"
               "2 2 0 5 1 3\n");
    Instance without_name;
    assert(instance_read("unnamed_instance.txt", &without_name, error,
                         sizeof error) == 0);
    assert(strcmp(without_name.name, "unnamed_instance") == 0);
    instance_free(&without_name);
    puts("Tous les tests passent.");
    return 0;
}
