#ifndef JSP_PSO_H
#define JSP_PSO_H

#include "instance.h"
#include "schedule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    DECODER_GT,
    DECODER_SEMI
} Decoder;

typedef enum {
    // Chaque particule suit le meilleur de ses voisins.
    TOPOLOGY_RING,
    // Toutes suivent le meilleur de l'essaim.
    TOPOLOGY_GLOBAL,
    // CLPSO : chaque dimension apprend d'une particule differente.
    TOPOLOGY_CL,
    // Un thread sur deux en "ring", les autres en "cl".
    TOPOLOGY_MIXED
} Topology;

typedef struct {
    double seconds;
    int target_makespan;
    uint64_t seed;
    int particles;
    // 0 = autant de threads que de coeurs disponibles.
    int threads;
    bool decreasing_inertia;
    Decoder decoder;
    double delta;
    Topology topology;
    int neighbors;
    // Nombre d'iterations sans progres avant de relancer l'essaim (0 = jamais).
    int restart;
    // Toutes les N iterations, un essaim recoit le meilleur des autres
    // threads (0 = essaims independants).
    int migrate;
    double max_velocity;
    const char *output_directory;
    // NULL ou "" : pas de journal.
    const char *log_path;
} SolverOptions;

void solver_options_default(SolverOptions *options);

// Lance un ou plusieurs essaims jusqu'a la limite ou la cible demandee.
// best doit avoir ete prepare par schedule_init ; il recoit le meilleur
// planning. Renvoie 0 en cas de succes, -1 sinon (message dans error).
int solve(const Instance *instance, const SolverOptions *options,
          Schedule *best, char *error, size_t error_size);

#endif
