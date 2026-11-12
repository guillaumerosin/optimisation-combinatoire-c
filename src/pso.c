#include "pso.h"

#include "platform.h"
#include "rng.h"

#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void solver_options_default(SolverOptions *options) {
    options->seconds = 55.0;
    options->target_makespan = -1;
    options->seed = 0;
    options->particles = 30;
    options->threads = 0;
    options->decreasing_inertia = true;
    options->decoder = DECODER_GT;
    options->delta = 0.2;
    options->topology = TOPOLOGY_MIXED;
    options->neighbors = 2;
    options->restart = 200;
    options->migrate = 50;
    options->max_velocity = 0.2;
    options->output_directory = ".";
    options->log_path = NULL;
}

typedef struct {
    double key;
    int index;
} KeyIndex;

// Tout ce qu'il faut pour evaluer une position sans allouer.
typedef struct {
    DecodeWork work;
    KeyIndex *pairs;
    int *sequence;
} Evaluator;

// Les particules d'un essaim sont stockees en blocs contigus : la
// particule i occupe position[i * dimension .. (i + 1) * dimension).
typedef struct {
    double *position;
    double *velocity;
    double *best_position;
    int *best_makespan;
    // CLPSO : exemplar[i * dimension + d] est la particule dont le meilleur
    // personnel guide la dimension d de i ; stale compte les iterations
    // sans progres.
    int *exemplar;
    unsigned char *has_exemplar;
    int *stale;
    // guide[i] est le meilleur voisin de i.
    int *guide;
} Swarm;

// Etat commun a tous les threads.
typedef struct {
    const Instance *instance;
    const SolverOptions *options;
    double start;
    // Negatif : pas de limite de temps.
    double deadline;
    // Leve quand la cible est atteinte ou qu'un thread echoue.
    atomic_bool stop;
    atomic_bool failed;
    atomic_llong evaluations;
    // Le makespan du record est lu sans verrou a chaque evaluation ; le
    // planning et la position du record sont proteges par le mutex.
    atomic_int best_makespan;
    pthread_mutex_t mutex;
    Schedule *best;
    double *best_position;
    int *best_starts;
    double last_write;
    FILE *log;
} Shared;

// Contexte d'un thread : remplace les captures des lambdas du C++.
typedef struct {
    Shared *shared;
    int id;
    int dimension;
    int size;
    Swarm swarm;
    Evaluator evaluator;
    Rng random;
} Worker;

static void set_error(char *error, size_t size, const char *format, ...) {
    if (!error || size == 0) return;
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(error, size, format, arguments);
    va_end(arguments);
}

static double clamp(double value, double low, double high) {
    return value < low ? low : value > high ? high : value;
}

static long long elapsed_ms(const Shared *shared) {
    return (long long)((monotonic_seconds() - shared->start) * 1000.0);
}

static int evaluator_init(Evaluator *evaluator, const Instance *instance) {
    const size_t dimension = (size_t)instance->jobs * (size_t)instance->machines;
    evaluator->pairs = malloc(dimension * sizeof *evaluator->pairs);
    evaluator->sequence = malloc(dimension * sizeof *evaluator->sequence);
    if (!evaluator->pairs || !evaluator->sequence ||
        decode_work_init(&evaluator->work, instance) != 0) {
        return -1;
    }
    return 0;
}

static void evaluator_free(Evaluator *evaluator) {
    free(evaluator->pairs);
    free(evaluator->sequence);
    decode_work_free(&evaluator->work);
}

// qsort n'est pas stable : on departage les cles egales par leur indice,
// ce qui reproduit std::stable_sort.
static int compare_keys(const void *left, const void *right) {
    const KeyIndex *a = left;
    const KeyIndex *b = right;
    if (a->key < b->key) return -1;
    if (a->key > b->key) return 1;
    return (a->index > b->index) - (a->index < b->index);
}

// Evalue une position. starts n'est rempli que lorsqu'il est demande
// (nouveau record), pour garder l'evaluation rapide.
static int evaluate(const Instance *instance, const SolverOptions *options,
                    Evaluator *evaluator, const double *keys, int *starts) {
    if (options->decoder == DECODER_SEMI) {
        // Decodeur "semi" : le tri des cles donne une sequence de jobs ;
        // l'indice divise par m donne le job.
        const int dimension = instance->jobs * instance->machines;
        for (int index = 0; index < dimension; ++index) {
            evaluator->pairs[index].key = keys[index];
            evaluator->pairs[index].index = index;
        }
        qsort(evaluator->pairs, (size_t)dimension, sizeof *evaluator->pairs,
              compare_keys);
        for (int index = 0; index < dimension; ++index) {
            evaluator->sequence[index] =
                evaluator->pairs[index].index / instance->machines;
        }
        return decode_semi_active(instance, evaluator->sequence,
                                  &evaluator->work, starts);
    }
    return decode_active(instance, keys, options->delta, &evaluator->work,
                         starts);
}

// Quelques particules partent de regles de priorite classiques
// (0 : SPT, 1 : MWKR, 2 : MOPNR) pour donner a l'essaim des points de
// depart structures.
static int priority_score(const Instance *instance, const int *next, int job,
                          int rule) {
    if (rule == 0) return -instance_op(instance, job, next[job])->duration;
    int value = 0;
    for (int operation = next[job]; operation < instance->machines;
         ++operation) {
        value += rule == 2 ? 1 : instance_op(instance, job, operation)->duration;
    }
    return value;
}

static int keys_from_priority(const Instance *instance, int rule,
                              double *keys) {
    const int jobs = instance->jobs;
    const int dimension = jobs * instance->machines;
    int *next = calloc((size_t)jobs, sizeof *next);
    int *occurrences = calloc((size_t)jobs, sizeof *occurrences);
    int *sequence = malloc((size_t)dimension * sizeof *sequence);
    if (!next || !occurrences || !sequence) {
        free(next);
        free(occurrences);
        free(sequence);
        return -1;
    }
    for (int step = 0; step < dimension; ++step) {
        int selected = -1;
        for (int job = 0; job < jobs; ++job) {
            if (next[job] >= instance->machines) continue;
            if (selected < 0 ||
                priority_score(instance, next, job, rule) >
                    priority_score(instance, next, selected, rule)) {
                selected = job;
            }
        }
        sequence[step] = selected;
        ++next[selected];
    }
    for (int rank = 0; rank < dimension; ++rank) {
        const int job = sequence[rank];
        const int operation = occurrences[job]++;
        keys[job * instance->machines + operation] =
            (double)rank / (double)dimension;
    }
    free(next);
    free(occurrences);
    free(sequence);
    return 0;
}

static int swarm_init(Swarm *swarm, int size, int dimension) {
    const size_t cells = (size_t)size * (size_t)dimension;
    swarm->position = malloc(cells * sizeof(double));
    swarm->velocity = malloc(cells * sizeof(double));
    swarm->best_position = malloc(cells * sizeof(double));
    swarm->best_makespan = malloc((size_t)size * sizeof(int));
    swarm->exemplar = malloc(cells * sizeof(int));
    swarm->has_exemplar = calloc((size_t)size, 1);
    swarm->stale = calloc((size_t)size, sizeof(int));
    swarm->guide = calloc((size_t)size, sizeof(int));
    if (!swarm->position || !swarm->velocity || !swarm->best_position ||
        !swarm->best_makespan || !swarm->exemplar || !swarm->has_exemplar ||
        !swarm->stale || !swarm->guide) {
        return -1;
    }
    return 0;
}

static void swarm_free(Swarm *swarm) {
    free(swarm->position);
    free(swarm->velocity);
    free(swarm->best_position);
    free(swarm->best_makespan);
    free(swarm->exemplar);
    free(swarm->has_exemplar);
    free(swarm->stale);
    free(swarm->guide);
}

// Appele mutex verrouille.
static void write_if_due(Shared *shared, bool force) {
    const double now = monotonic_seconds();
    // Une ecriture periodique protege le meilleur resultat si le programme
    // est interrompu, sans ralentir chaque evaluation.
    if (!force && now - shared->last_write < 2.0) return;
    shared->last_write = now;
    const char *directory = shared->options->output_directory;
    const size_t length =
        strlen(directory) + strlen(shared->instance->name) + 7;
    char *path = malloc(length);
    if (!path) return;
    snprintf(path, length, "%s/%s.json", directory, shared->instance->name);
    if (make_directories(directory) != 0 ||
        schedule_write_json(shared->best, shared->instance, path) != 0) {
        fprintf(stderr, "Erreur: Impossible d'ecrire le resultat: %s\n", path);
    }
    free(path);
}

static void publish(Worker *worker, const double *position, int makespan) {
    Shared *shared = worker->shared;
    if (makespan >= atomic_load(&shared->best_makespan)) return;
    pthread_mutex_lock(&shared->mutex);
    if (makespan < shared->best->makespan) {
        const Instance *instance = shared->instance;
        evaluate(instance, shared->options, &worker->evaluator, position,
                 shared->best_starts);
        schedule_from_starts(instance, shared->best_starts, shared->best);
        memcpy(shared->best_position, position,
               (size_t)worker->dimension * sizeof(double));
        atomic_store(&shared->best_makespan, shared->best->makespan);
        printf("[progression] temps_ms=%lld makespan=%d thread=%d\n",
               elapsed_ms(shared), shared->best->makespan, worker->id);
        fflush(stdout);
        write_if_due(shared, false);
        if (shared->log) {
            fprintf(shared->log, "%lld;%d\n", elapsed_ms(shared),
                    shared->best->makespan);
        }
        const int target = shared->options->target_makespan;
        if (target > 0 && shared->best->makespan <= target) {
            atomic_store(&shared->stop, true);
        }
    }
    pthread_mutex_unlock(&shared->mutex);
}

static void randomize(Worker *worker, int index) {
    Swarm *swarm = &worker->swarm;
    const int dimension = worker->dimension;
    const double max_velocity = worker->shared->options->max_velocity;
    double *position = swarm->position + (size_t)index * dimension;
    double *velocity = swarm->velocity + (size_t)index * dimension;
    for (int key = 0; key < dimension; ++key) {
        position[key] = rng_unit(&worker->random);
        velocity[key] =
            -max_velocity + 2.0 * max_velocity * rng_unit(&worker->random);
    }
    memcpy(swarm->best_position + (size_t)index * dimension, position,
           (size_t)dimension * sizeof(double));
    swarm->best_makespan[index] =
        evaluate(worker->shared->instance, worker->shared->options,
                 &worker->evaluator, position, NULL);
    swarm->has_exemplar[index] = 0;
    swarm->stale[index] = 0;
}

static int tournament(Worker *worker, int owner) {
    const int size = worker->size;
    int first = rng_below(&worker->random, size);
    int second = rng_below(&worker->random, size);
    if (size > 1) {
        while (first == owner) first = rng_below(&worker->random, size);
        while (second == owner) second = rng_below(&worker->random, size);
    }
    const int *best = worker->swarm.best_makespan;
    return best[first] <= best[second] ? first : second;
}

// CLPSO (Liang et al., 2006) : chaque dimension apprend du meilleur
// personnel d'une particule tiree par tournoi, avec une probabilite
// propre a chaque particule. Les particules n'imitent donc jamais
// toutes le meilleur, ce qui garde la diversite en grande dimension.
static void choose_exemplars(Worker *worker, int owner) {
    const int size = worker->size;
    const int dimension = worker->dimension;
    const double learning = size <= 1 ? 0.0 :
        0.05 + 0.45 * (exp(10.0 * owner / (size - 1)) - 1.0) /
                          (exp(10.0) - 1.0);
    int *exemplar = worker->swarm.exemplar + (size_t)owner * dimension;
    bool learns = false;
    for (int key = 0; key < dimension; ++key) {
        exemplar[key] = owner;
        if (rng_unit(&worker->random) < learning) {
            exemplar[key] = tournament(worker, owner);
            learns = true;
        }
    }
    if (!learns && size > 1) {
        exemplar[rng_below(&worker->random, dimension)] =
            tournament(worker, owner);
    }
    worker->swarm.has_exemplar[owner] = 1;
    worker->swarm.stale[owner] = 0;
}

// Les guides sont calcules au debut de chaque iteration a partir des
// meilleurs personnels. Renvoie l'indice du meilleur de l'essaim.
static int compute_guides(Worker *worker) {
    const SolverOptions *options = worker->shared->options;
    const int size = worker->size;
    const int *best_makespan = worker->swarm.best_makespan;
    int *guide = worker->swarm.guide;
    int best = 0;
    for (int index = 1; index < size; ++index) {
        if (best_makespan[index] < best_makespan[best]) best = index;
    }
    for (int index = 0; index < size; ++index) {
        if (options->topology == TOPOLOGY_GLOBAL) {
            guide[index] = best;
            continue;
        }
        // Anneau : l'information circule de voisin en voisin, ce qui
        // retarde la convergence de tout l'essaim vers un meme point.
        int chosen = index;
        for (int offset = -options->neighbors; offset <= options->neighbors;
             ++offset) {
            const int other = ((index + offset) % size + size) % size;
            if (best_makespan[other] < best_makespan[chosen]) chosen = other;
        }
        guide[index] = chosen;
    }
    return best;
}

static void run_worker(Worker *worker) {
    Shared *shared = worker->shared;
    const Instance *instance = shared->instance;
    const SolverOptions *options = shared->options;
    Swarm *swarm = &worker->swarm;
    const int dimension = worker->dimension;
    const int size = worker->size;
    const double max_velocity = options->max_velocity;

    for (int index = 0; index < size; ++index) {
        randomize(worker, index);
        double *position = swarm->position + (size_t)index * dimension;
        double *best_position = swarm->best_position + (size_t)index * dimension;
        // Seul le premier essaim recoit les regles de priorite : les
        // autres restent aleatoires pour diversifier les threads.
        if (worker->id == 0 && index < 3) {
            if (keys_from_priority(instance, index, position) != 0) {
                atomic_store(&shared->failed, true);
                return;
            }
            memcpy(best_position, position, (size_t)dimension * sizeof(double));
            swarm->best_makespan[index] = evaluate(
                instance, options, &worker->evaluator, position, NULL);
        }
        publish(worker, best_position, swarm->best_makespan[index]);
    }

    int swarm_best = swarm->best_makespan[compute_guides(worker)];
    int stagnation = 0;
    long long iteration = 0;
    const double c = 1.49618;
    // "mixed" alterne les deux variantes d'un thread a l'autre : CLPSO
    // explore mieux les petites instances, l'anneau converge plus vite
    // sur les grandes, et la migration partage le record entre eux.
    const bool comprehensive = options->topology == TOPOLOGY_CL ||
        (options->topology == TOPOLOGY_MIXED && worker->id % 2 == 1);
    for (;;) {
        const double now = monotonic_seconds();
        if ((shared->deadline >= 0.0 && now >= shared->deadline) ||
            atomic_load(&shared->stop)) {
            break;
        }
        ++iteration;
        atomic_fetch_add(&shared->evaluations, size);
        const double ratio = options->seconds == 0.0 ? 0.0
            : fmin(1.0, (now - shared->start) / options->seconds);
        const double inertia = options->decreasing_inertia
            ? 0.9 - 0.5 * ratio : 0.7298;
        const int best_index = compute_guides(worker);

        for (int index = 0; index < size; ++index) {
            double *position = swarm->position + (size_t)index * dimension;
            double *velocity = swarm->velocity + (size_t)index * dimension;
            double *best_position =
                swarm->best_position + (size_t)index * dimension;
            if (comprehensive) {
                if (!swarm->has_exemplar[index] || swarm->stale[index] >= 7) {
                    choose_exemplars(worker, index);
                }
                const int *exemplar =
                    swarm->exemplar + (size_t)index * dimension;
                for (int key = 0; key < dimension; ++key) {
                    const double target = swarm->best_position
                        [(size_t)exemplar[key] * dimension + key];
                    double speed = inertia * velocity[key] +
                        1.49445 * rng_unit(&worker->random) *
                            (target - position[key]);
                    speed = clamp(speed, -max_velocity, max_velocity);
                    velocity[key] = speed;
                    position[key] += speed;
                }
            } else {
                const double *social = swarm->best_position +
                    (size_t)swarm->guide[index] * dimension;
                // Mise a jour PSO : inertie, attraction vers le meilleur
                // personnel puis vers le meilleur du voisinage.
                for (int key = 0; key < dimension; ++key) {
                    double speed = inertia * velocity[key] +
                        c * rng_unit(&worker->random) *
                            (best_position[key] - position[key]) +
                        c * rng_unit(&worker->random) *
                            (social[key] - position[key]);
                    speed = clamp(speed, -max_velocity, max_velocity);
                    velocity[key] = speed;
                    position[key] += speed;
                }
            }
            const int makespan =
                evaluate(instance, options, &worker->evaluator, position, NULL);
            if (makespan < swarm->best_makespan[index]) {
                swarm->stale[index] = 0;
            } else {
                ++swarm->stale[index];
            }
            // A makespan egal, on accepte quand meme le deplacement :
            // le JSP a de larges plateaux et cela evite de s'y figer.
            if (makespan <= swarm->best_makespan[index]) {
                swarm->best_makespan[index] = makespan;
                memcpy(best_position, position,
                       (size_t)dimension * sizeof(double));
                publish(worker, position, makespan);
            }
        }

        const int current = swarm->best_makespan[best_index];
        if (current < swarm_best) {
            swarm_best = current;
            stagnation = 0;
        } else {
            ++stagnation;
        }

        if (options->migrate > 0 && iteration % options->migrate == 0 &&
            atomic_load(&shared->best_makespan) < swarm_best) {
            // Multi-essaims : la pire particule adopte le record global.
            int worst = 0;
            for (int index = 1; index < size; ++index) {
                if (swarm->best_makespan[index] > swarm->best_makespan[worst]) {
                    worst = index;
                }
            }
            const size_t bytes = (size_t)dimension * sizeof(double);
            pthread_mutex_lock(&shared->mutex);
            memcpy(swarm->position + (size_t)worst * dimension,
                   shared->best_position, bytes);
            memcpy(swarm->best_position + (size_t)worst * dimension,
                   shared->best_position, bytes);
            swarm->best_makespan[worst] = shared->best->makespan;
            pthread_mutex_unlock(&shared->mutex);
        }

        // CLPSO entretient deja sa diversite : seul l'anneau redemarre.
        if (!comprehensive && options->restart > 0 &&
            stagnation >= options->restart) {
            // Redemarrage partiel : l'essaim a converge, on garde sa
            // meilleure particule et on relance toutes les autres.
            const int keep = compute_guides(worker);
            for (int index = 0; index < size; ++index) {
                if (index != keep) randomize(worker, index);
            }
            stagnation = 0;
        }
    }
}

// Chaque worker possede son propre essaim et son propre generateur
// aleatoire. Seul le meilleur global est partage entre les threads.
static void *worker_main(void *argument) {
    // Copie locale : les Worker sont contigus dans un meme tableau, et le
    // generateur, modifie sans cesse, partagerait sinon des lignes de cache
    // avec celui du thread voisin (false sharing).
    Worker local = *(const Worker *)argument;
    Worker *worker = &local;
    Shared *shared = worker->shared;
    rng_seed(&worker->random, shared->options->seed +
             0x9e3779b97f4a7c15ULL * (uint64_t)(worker->id + 1));
    if (swarm_init(&worker->swarm, worker->size, worker->dimension) != 0 ||
        evaluator_init(&worker->evaluator, shared->instance) != 0) {
        atomic_store(&shared->failed, true);
    } else {
        run_worker(worker);
    }
    // Un thread en echec arrete les autres : sans limite de temps, ils
    // tourneraient sinon indefiniment.
    if (atomic_load(&shared->failed)) atomic_store(&shared->stop, true);
    swarm_free(&worker->swarm);
    evaluator_free(&worker->evaluator);
    return NULL;
}

int solve(const Instance *instance, const SolverOptions *options,
          Schedule *best, char *error, size_t error_size) {
    const int dimension = instance->jobs * instance->machines;
    // Un essaim par coeur : plus d'essaims, c'est plus de diversite pour le
    // meme temps de calcul.
    const int threads = options->threads > 0 ? options->threads : cpu_count();
    int status = -1;
    int started = 0;
    Shared shared;
    memset(&shared, 0, sizeof shared);
    shared.instance = instance;
    shared.options = options;
    shared.start = monotonic_seconds();
    // --time 0 signifie "pas de limite". Dans ce cas, --target peut arreter
    // la recherche lorsqu'un makespan donne est atteint.
    shared.deadline = options->seconds == 0.0
        ? -1.0 : shared.start + options->seconds;
    atomic_init(&shared.stop, false);
    atomic_init(&shared.failed, false);
    atomic_init(&shared.evaluations, 0);
    atomic_init(&shared.best_makespan, INT_MAX);
    pthread_mutex_init(&shared.mutex, NULL);
    best->makespan = INT_MAX;
    shared.best = best;
    shared.last_write = shared.start - 2.0;

    shared.best_position = malloc((size_t)dimension * sizeof(double));
    shared.best_starts = malloc((size_t)dimension * sizeof(int));
    Worker *workers = calloc((size_t)threads, sizeof *workers);
    pthread_t *handles = calloc((size_t)threads, sizeof *handles);
    if (!shared.best_position || !shared.best_starts || !workers || !handles) {
        set_error(error, error_size, "Memoire insuffisante");
        goto cleanup;
    }
    if (options->log_path && *options->log_path) {
        shared.log = fopen(options->log_path, "w");
        if (shared.log) fputs("temps_ms;meilleur_makespan\n", shared.log);
    }

    for (int id = 0; id < threads; ++id) {
        workers[id].shared = &shared;
        workers[id].id = id;
        workers[id].dimension = dimension;
        workers[id].size = options->particles > 1 ? options->particles : 1;
        if (pthread_create(&handles[id], NULL, worker_main, &workers[id]) != 0) {
            atomic_store(&shared.failed, true);
            atomic_store(&shared.stop, true);
            break;
        }
        ++started;
    }
    for (int id = 0; id < started; ++id) {
        pthread_join(handles[id], NULL);
    }
    if (atomic_load(&shared.failed)) {
        set_error(error, error_size,
                  "Memoire insuffisante ou creation de thread impossible");
        goto cleanup;
    }
    write_if_due(&shared, true);
    printf("[fin] evaluations=%lld\n", (long long)atomic_load(&shared.evaluations));
    fflush(stdout);
    status = 0;

cleanup:
    if (shared.log) fclose(shared.log);
    pthread_mutex_destroy(&shared.mutex);
    free(shared.best_position);
    free(shared.best_starts);
    free(workers);
    free(handles);
    return status;
}
