#ifndef JSP_INSTANCE_H
#define JSP_INSTANCE_H

#include <stddef.h>

typedef struct {
    int machine;
    int duration;
} Operation;

typedef struct {
    char name[256];
    int jobs;
    int machines;
    // Tableau plat : l'operation k du job j est ops[j * machines + k].
    Operation *ops;
} Instance;

// Lit une instance texte dans le format jobs, machines, puis recettes.
// Renvoie 0 en cas de succes, -1 sinon (le message est ecrit dans error).
int instance_read(const char *path, Instance *out, char *error,
                  size_t error_size);
void instance_free(Instance *instance);

static inline const Operation *instance_op(const Instance *instance, int job,
                                           int operation) {
    return &instance->ops[(size_t)job * (size_t)instance->machines +
                          (size_t)operation];
}

#endif
