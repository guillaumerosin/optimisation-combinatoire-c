#ifndef JSP_SCHEDULE_H
#define JSP_SCHEDULE_H

#include "instance.h"

typedef struct {
    int machine;
    int start;
    int end;
} ScheduledOperation;

typedef struct {
    int makespan;
    int jobs;
    int machines;
    // ops[job * machines + operation], dans l'ordre de la recette.
    ScheduledOperation *ops;
} Schedule;

// Tampons des decodeurs. Le decodeur est appele des millions de fois :
// chaque thread alloue les siens une fois pour toutes.
typedef struct {
    int *next;
    int *job_ready;
    int *machine_ready;
    int *earliest;
    int *machine_earliest;
    // current_machine[job] vaut -1 quand le job est termine.
    int *current_machine;
} DecodeWork;

int decode_work_init(DecodeWork *work, const Instance *instance);
void decode_work_free(DecodeWork *work);

int schedule_init(Schedule *schedule, const Instance *instance);
void schedule_free(Schedule *schedule);

// Decode une sequence de jobs (taille jobs * machines) en respectant les
// dates de disponibilite. Renvoie le makespan, ou -1 si la sequence est
// invalide. Si starts n'est pas nul, il recoit la date de debut de chaque
// operation.
int decode_semi_active(const Instance *instance, const int *sequence,
                       DecodeWork *work, int *starts);
// Giffler-Thompson parametre : priorities[job * m + operation] departage les
// operations en conflit (plus petite cle = prioritaire). delta = 1 donne un
// ordonnancement actif, delta = 0 un ordonnancement sans retard.
int decode_active(const Instance *instance, const double *priorities,
                  double delta, DecodeWork *work, int *starts);
// Construit le planning complet a partir des dates de debut.
void schedule_from_starts(const Instance *instance, const int *starts,
                          Schedule *out);
// Ecrit uniquement le format JSON attendu par le concours.
// Renvoie 0 en cas de succes, -1 si le fichier ne peut pas etre ecrit.
int schedule_write_json(const Schedule *schedule, const Instance *instance,
                        const char *path);

#endif
