#include "instance.h"

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_error(char *error, size_t size, const char *format, ...) {
    if (!error || size == 0) return;
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(error, size, format, arguments);
    va_end(arguments);
}

static int is_blank(char c) {
    return c == ' ' || c == '\t' || c == '\r';
}

static int is_comment_or_empty(const char *line) {
    while (is_blank(*line)) ++line;
    return *line == '\0' || *line == '#';
}

// Retire les espaces et le '\r' des fichiers Windows autour du nom,
// sinon le fichier de sortie s'appellerait "10_10\r.json".
static void copy_trimmed(char *dest, size_t size, const char *text) {
    while (is_blank(*text)) ++text;
    size_t length = strlen(text);
    while (length > 0 && is_blank(text[length - 1])) --length;
    if (length >= size) length = size - 1;
    memcpy(dest, text, length);
    dest[length] = '\0';
}

// Equivalent de std::filesystem::path(path).stem() : nom sans dossier ni
// extension.
static void copy_stem(char *dest, size_t size, const char *path) {
    const char *base = path;
    for (const char *c = path; *c; ++c) {
        if (*c == '/' || *c == '\\') base = c + 1;
    }
    const char *dot = strrchr(base, '.');
    size_t length = dot && dot != base ? (size_t)(dot - base) : strlen(base);
    if (length >= size) length = size - 1;
    memcpy(dest, base, length);
    dest[length] = '\0';
}

// Lit au plus capacity entiers. Renvoie leur nombre, capacity + 1 s'il en
// reste d'autres, ou -1 si un mot n'est pas un entier.
static int parse_ints(const char *text, long *values, int capacity) {
    int count = 0;
    for (;;) {
        while (is_blank(*text)) ++text;
        if (*text == '\0') return count;
        if (count == capacity) return capacity + 1;
        char *end = NULL;
        errno = 0;
        const long value = strtol(text, &end, 10);
        if (end == text || errno == ERANGE ||
            (*end != '\0' && !is_blank(*end))) {
            return -1;
        }
        values[count++] = value;
        text = end;
    }
}

// Charge tout le fichier en memoire, termine par un '\0'.
static char *read_file(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    char *content = NULL;
    size_t length = 0;
    size_t capacity = 0;
    char chunk[4096];
    size_t got;
    while ((got = fread(chunk, 1, sizeof chunk, file)) > 0) {
        if (length + got + 1 > capacity) {
            capacity = (length + got + 1) * 2;
            char *grown = realloc(content, capacity);
            if (!grown) {
                free(content);
                fclose(file);
                return NULL;
            }
            content = grown;
        }
        memcpy(content + length, chunk, got);
        length += got;
    }
    fclose(file);
    if (!content) content = calloc(1, 1);
    else content[length] = '\0';
    return content;
}

int instance_read(const char *path, Instance *out, char *error,
                  size_t error_size) {
    memset(out, 0, sizeof *out);
    int status = -1;
    char **lines = NULL;
    long *values = NULL;
    unsigned char *machine_used = NULL;

    char *content = read_file(path);
    if (!content) {
        set_error(error, error_size, "Impossible d'ouvrir l'instance: %s",
                  path);
        goto cleanup;
    }

    // On nettoie d'abord le fichier pour accepter les commentaires et
    // les lignes vides, y compris avec des fins de ligne Windows.
    size_t capacity = 1;
    for (const char *c = content; *c; ++c) {
        if (*c == '\n') ++capacity;
    }
    lines = malloc(capacity * sizeof *lines);
    if (!lines) {
        set_error(error, error_size, "Memoire insuffisante");
        goto cleanup;
    }
    size_t count = 0;
    char *line = content;
    for (char *c = content;; ++c) {
        if (*c != '\n' && *c != '\0') continue;
        const int last = *c == '\0';
        *c = '\0';
        if (!is_comment_or_empty(line)) lines[count++] = line;
        if (last) break;
        line = c + 1;
    }
    if (count == 0) {
        set_error(error, error_size, "Instance vide: %s", path);
        goto cleanup;
    }

    // Certaines instances JSPLIB ont un nom en premiere ligne, d'autres
    // commencent directement par les dimensions jobs x machines.
    long dimensions[2];
    size_t index = 0;
    if (parse_ints(lines[0], dimensions, 2) == 2) {
        copy_stem(out->name, sizeof out->name, path);
    } else {
        copy_trimmed(out->name, sizeof out->name, lines[0]);
        ++index;
    }
    if (index >= count) {
        set_error(error, error_size, "Dimensions absentes dans l'instance");
        goto cleanup;
    }
    if (parse_ints(lines[index++], dimensions, 2) != 2 ||
        dimensions[0] <= 0 || dimensions[1] <= 0 ||
        dimensions[1] > (INT_MAX - 1) / 2 ||
        dimensions[0] > INT_MAX / dimensions[1]) {
        set_error(error, error_size, "Dimensions invalides dans l'instance");
        goto cleanup;
    }
    out->jobs = (int)dimensions[0];
    out->machines = (int)dimensions[1];

    const int machines = out->machines;
    out->ops = malloc((size_t)out->jobs * (size_t)machines * sizeof *out->ops);
    values = malloc((size_t)(2 * machines + 1) * sizeof *values);
    machine_used = malloc((size_t)machines);
    if (!out->ops || !values || !machine_used) {
        set_error(error, error_size, "Memoire insuffisante");
        goto cleanup;
    }
    for (int job = 0; job < out->jobs; ++job) {
        if (index >= count) {
            set_error(error, error_size, "Ligne manquante pour le job %d", job);
            goto cleanup;
        }
        const int found = parse_ints(lines[index++], values, 2 * machines);
        if (found > 2 * machines) {
            set_error(error, error_size, "Trop de nombres pour le job %d", job);
            goto cleanup;
        }
        if (found != 2 * machines) {
            set_error(error, error_size, "Operation invalide pour le job %d",
                      job);
            goto cleanup;
        }
        memset(machine_used, 0, (size_t)machines);
        for (int operation = 0; operation < machines; ++operation) {
            const long machine = values[2 * operation];
            const long duration = values[2 * operation + 1];
            if (machine < 0 || machine >= machines || duration < 0 ||
                duration > INT_MAX) {
                set_error(error, error_size,
                          "Operation invalide pour le job %d", job);
                goto cleanup;
            }
            // Dans un job shop, chaque job passe une seule fois par machine.
            if (machine_used[machine]) {
                set_error(error, error_size,
                          "Machine %ld utilisee deux fois par le job %d",
                          machine, job);
                goto cleanup;
            }
            machine_used[machine] = 1;
            Operation *value =
                &out->ops[(size_t)job * (size_t)machines + (size_t)operation];
            value->machine = (int)machine;
            value->duration = (int)duration;
        }
    }
    // Des lignes en trop ne bloquent pas la resolution : on previent
    // seulement, pour ne pas perdre une instance le jour du concours.
    if (index < count) {
        fprintf(stderr,
                "Attention: %zu ligne(s) ignoree(s) apres le dernier job\n",
                count - index);
    }
    status = 0;

cleanup:
    free(content);
    free(lines);
    free(values);
    free(machine_used);
    if (status != 0) instance_free(out);
    return status;
}

void instance_free(Instance *instance) {
    free(instance->ops);
    instance->ops = NULL;
}
