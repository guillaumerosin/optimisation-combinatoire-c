#ifndef JSP_PLATFORM_H
#define JSP_PLATFORM_H

// Le peu de code qui depend du systeme est isole ici, pour que
// <windows.h> et ses macros ne fuient pas dans le reste du projet.

// Horloge monotone, en secondes.
double monotonic_seconds(void);
// Nombre de coeurs disponibles (au moins 1).
int cpu_count(void);
// Equivalent de std::filesystem::create_directories. Renvoie 0 si le
// dossier existe a la fin, -1 sinon.
int make_directories(const char *path);

#endif
