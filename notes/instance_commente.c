/*
 * instance_commente.c
 * ===================
 * Copie de src/instance.c, avec le MEME code, mais commentee ligne par ligne
 * pour apprendre. Ce fichier n'est pas compile avec le projet.
 *
 * Role du fichier : lire un fichier d'instance de job shop comme celui-ci
 *
 *     # commentaire (ignore)
 *     abz5                      <- nom (facultatif)
 *     10 10                     <- nombre de jobs, nombre de machines
 *     4 88 8 68 6 94 ...        <- job 0 : (machine, duree) x machines
 *     5 72 3 50 6 69 ...        <- job 1
 *     ...
 *
 * et remplir une struct Instance (voir instance.h) :
 *
 *     typedef struct {
 *         char name[256];       // nom de l'instance
 *         int jobs;             // nombre de jobs
 *         int machines;         // nombre de machines
 *         Operation *ops;       // jobs * machines operations, en tableau plat
 *     } Instance;
 *
 * Plan du fichier :
 *   1. petites fonctions utilitaires (erreurs, espaces, nom, nombres)
 *   2. read_file      : charge TOUT le fichier en memoire
 *   3. instance_read  : decoupe en lignes, lit les nombres, remplit Instance
 *   4. instance_free  : libere la memoire
 */

#include "instance.h"   // nos types : Instance, Operation

#include <errno.h>      // errno, ERANGE : detecter un nombre trop grand
#include <limits.h>     // INT_MAX : plus grand int possible
#include <stdarg.h>     // va_list : fonctions a nombre d'arguments variable
#include <stdio.h>      // FILE, fopen, fread, fclose, fprintf, vsnprintf
#include <stdlib.h>     // malloc, calloc, realloc, free, strtol
#include <string.h>     // strlen, strrchr, memcpy, memset

/* =========================================================================
 * 1. UTILITAIRES
 * ========================================================================= */

/*
 * set_error : ecrit un message d'erreur dans le tampon fourni par l'appelant.
 *
 * En C il n'y a pas d'exceptions. Notre convention :
 *   - la fonction renvoie -1 en cas d'erreur ;
 *   - le texte de l'erreur est ecrit dans un tableau "error" que l'APPELANT
 *     a cree (ex : char error[256]) et dont il nous donne la taille.
 *
 * Les "..." veulent dire "autant d'arguments qu'on veut", comme printf :
 *   set_error(error, size, "Ligne manquante pour le job %d", job);
 */
static void set_error(char *error, size_t size, const char *format, ...) {
    // Si l'appelant n'a pas fourni de tampon, on ne fait rien.
    if (!error || size == 0) return;
    va_list arguments;              // "curseur" sur les arguments variables
    va_start(arguments, format);    // il commence apres le parametre format
    // vsnprintf = printf, mais :
    //   - ecrit dans une chaine (error) au lieu de l'ecran ;
    //   - "n" : n'ecrit JAMAIS plus de size octets (pas de debordement) ;
    //   - "v" : prend un va_list au lieu de "..." .
    vsnprintf(error, size, format, arguments);
    va_end(arguments);              // obligatoire apres va_start
}

/*
 * is_blank : vrai (1) si c est un "blanc" : espace, tabulation ou '\r'.
 * Le '\r' vient des fichiers Windows, ou chaque ligne finit par "\r\n".
 * En le traitant comme un espace, on n'a plus jamais a s'en soucier.
 */
static int is_blank(char c) {
    return c == ' ' || c == '\t' || c == '\r';
}

/*
 * is_comment_or_empty : vrai si la ligne est vide, ne contient que des
 * blancs, ou commence (apres les blancs) par '#'.
 *
 * "const char *line" : un pointeur vers le premier caractere de la ligne.
 * "const" promet qu'on ne modifiera pas les caracteres.
 */
static int is_comment_or_empty(const char *line) {
    // On avance le pointeur tant qu'il pointe sur un blanc.
    // *line = "le caractere pointe" ; ++line = "passe au suivant".
    while (is_blank(*line)) ++line;
    // '\0' = fin de chaine : la ligne n'avait que des blancs.
    return *line == '\0' || *line == '#';
}

/*
 * copy_trimmed : copie "text" dans "dest" sans les blancs du debut et de la
 * fin. Sans ca, un nom lu sous Windows vaudrait "abz5\r", et le fichier de
 * sortie s'appellerait "abz5\r.json".
 *
 * "dest" est un tableau de "size" octets : on ne doit JAMAIS ecrire plus.
 */
static void copy_trimmed(char *dest, size_t size, const char *text) {
    // Saute les blancs du debut.
    while (is_blank(*text)) ++text;
    // strlen compte les caracteres jusqu'au '\0' (non compris).
    size_t length = strlen(text);
    // Recule tant que le dernier caractere est un blanc.
    // text[length - 1] = dernier caractere. On teste length > 0 d'abord
    // pour ne pas lire text[-1] si la chaine est vide.
    while (length > 0 && is_blank(text[length - 1])) --length;
    // Si le nom est trop long pour dest, on le coupe.
    // size - 1 car il faut garder une case pour le '\0' final.
    if (length >= size) length = size - 1;
    // memcpy(destination, source, nombre_d_octets) : copie brute.
    memcpy(dest, text, length);
    // Une chaine C se termine TOUJOURS par '\0'. memcpy ne l'ajoute pas.
    dest[length] = '\0';
}

/*
 * copy_stem : extrait le nom d'un fichier sans dossier ni extension.
 *   "instances/abz5"          -> "abz5"
 *   "C:\\data\\ft10.txt"      -> "ft10"
 * Sert quand le fichier n'a pas de ligne de nom.
 */
static void copy_stem(char *dest, size_t size, const char *path) {
    // base pointera juste apres le dernier '/' ou '\'.
    const char *base = path;
    for (const char *c = path; *c; ++c) {      // *c != '\0' : fin de chaine
        if (*c == '/' || *c == '\\') base = c + 1;
    }
    // strrchr cherche la DERNIERE occurrence de '.' (NULL si aucune).
    const char *dot = strrchr(base, '.');
    // Si un point existe (et n'est pas au tout debut, ex : ".config"), le
    // nom s'arrete au point. "dot - base" = nombre de caracteres entre les
    // deux pointeurs (soustraction de pointeurs).
    size_t length = dot && dot != base ? (size_t)(dot - base) : strlen(base);
    if (length >= size) length = size - 1;
    memcpy(dest, base, length);
    dest[length] = '\0';
}

/*
 * parse_ints : lit les entiers d'une ligne de texte.
 *
 *   text     : la ligne, ex "4 88 8 68"
 *   values   : tableau ou ranger les nombres lus
 *   capacity : taille de values (on n'ecrit jamais au-dela)
 *
 * Valeur de retour :
 *   - le nombre d'entiers lus           ("4 88 8 68" -> 4)
 *   - capacity + 1 s'il y en a TROP     (permet de detecter "trop long")
 *   - -1 si un mot n'est pas un entier  ("4 a 8"     -> -1)
 */
static int parse_ints(const char *text, long *values, int capacity) {
    int count = 0;                      // nombre d'entiers deja ranges
    for (;;) {                          // boucle infinie, on sort par return
        while (is_blank(*text)) ++text; // saute les espaces avant le nombre
        if (*text == '\0') return count;    // fin de ligne : termine
        // Il reste quelque chose mais values est plein : trop de nombres.
        if (count == capacity) return capacity + 1;

        /*
         * strtol ("string to long") convertit le texte en nombre.
         *   strtol("88 8 68", &end, 10) renvoie 88
         *   et fait pointer end sur " 8 68" (juste apres le nombre).
         * C'est "end" qui nous permet d'AVANCER dans la ligne.
         * 10 = base decimale.
         */
        char *end = NULL;
        errno = 0;                      // strtol met errno a ERANGE si le
        const long value = strtol(text, &end, 10);  // nombre est trop grand
        if (end == text                 // rien lu : ce n'est pas un nombre
            || errno == ERANGE          // nombre trop grand pour un long
            || (*end != '\0' && !is_blank(*end))) { // "12abc" : colle a du
            return -1;                              // texte -> invalide
        }
        values[count++] = value;        // range puis incremente count
        text = end;                     // reprend apres le nombre lu
    }
}

/* =========================================================================
 * 2. read_file : CHARGER TOUT LE FICHIER EN MEMOIRE
 * =========================================================================
 *
 * Probleme : on ne connait pas la taille du fichier a l'avance, et un
 * tableau C a une taille FIXE. Solution : lire par petits morceaux de
 * 4096 octets et agrandir un tampon dynamique (malloc/realloc) au fur et
 * a mesure. C'est ce que fait std::vector en C++, mais ici a la main.
 *
 * Trois variables decrivent le tampon "content" :
 *
 *   content  : adresse du tampon (NULL tant que rien n'est alloue)
 *   length   : combien d'octets sont REMPLIS (ce qu'on a deja lu)
 *   capacity : combien d'octets sont RESERVES (la place totale)
 *
 *   content : [ a b z 5 \n 1 0   1 0 \n . . . . . . . . . . . ]
 *              |<------- length ------->|
 *              |<-------------------- capacity ------------------->|
 *
 * Toujours : length <= capacity. Les cases entre length et capacity sont
 * reservees mais pas encore utilisees.
 *
 * Renvoie le contenu termine par '\0' (a liberer avec free), ou NULL si
 * le fichier n'existe pas ou si la memoire manque.
 */
static char *read_file(const char *path) {
    // fopen ouvre le fichier. "rb" = lecture (r), en binaire (b) : on
    // recoit les octets tels quels, '\r' compris, sur tous les systemes.
    // Renvoie NULL si le fichier n'existe pas : il FAUT le verifier.
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;

    char *content = NULL;   // rien d'alloue au depart
    size_t length = 0;      // 0 octet rempli
    size_t capacity = 0;    // 0 octet reserve

    // Petit tableau temporaire sur la pile : fread y depose chaque morceau.
    char chunk[4096];
    // got = nombre d'octets que fread a REELLEMENT lus a ce tour.
    // (4096 en general, moins au dernier morceau, 0 a la fin du fichier.)
    size_t got;

    // A chaque tour : lire jusqu'a 4096 octets dans chunk.
    //   fread(ou_ecrire, taille_d_un_element, nb_max_d_elements, fichier)
    // fread renvoie le nombre d'elements lus ; 0 = fin du fichier -> stop.
    // Les parentheses autour de (got = fread(...)) sont indispensables :
    // on range d'abord le resultat dans got, PUIS on le compare a 0.
    while ((got = fread(chunk, 1, sizeof chunk, file)) > 0) {

        /*
         * LA LIGNE CLE : y a-t-il assez de place dans content ?
         *
         * On veut stocker dans content :
         *     length   octets deja presents
         *   + got      octets qu'on vient de lire dans chunk
         *   + 1        octet pour le '\0' qu'on ajoutera a la toute fin
         *
         * Si ce total depasse la place reservee (capacity), il faut
         * agrandir AVANT de copier, sinon memcpy ecrirait hors du tampon.
         *
         * Exemple avec un fichier de 10 000 octets :
         *   tour 1 : length=0,    got=4096 -> besoin 4097  > capacity 0
         *            -> on agrandit : capacity = 4097 * 2 = 8194
         *   tour 2 : length=4096, got=4096 -> besoin 8193 <= 8194
         *            -> assez de place, pas d'agrandissement
         *   tour 3 : length=8192, got=1808 -> besoin 10001 > 8194
         *            -> on agrandit : capacity = 10001 * 2 = 20002
         *   tour 4 : fread renvoie 0 -> fin de la boucle, length = 10000
         */
        if (length + got + 1 > capacity) {
            // On ne prend pas juste ce qu'il faut : on DOUBLE. Ainsi on
            // appelle realloc rarement (realloc peut recopier tout le
            // tampon, c'est couteux).
            capacity = (length + got + 1) * 2;

            /*
             * realloc(ancien_bloc, nouvelle_taille) :
             *   - agrandit le bloc, en le DEPLACANT ailleurs si besoin
             *     (l'ancien contenu est recopie automatiquement) ;
             *   - renvoie la (peut-etre nouvelle) adresse ;
             *   - realloc(NULL, n) se comporte comme malloc(n) : c'est ce
             *     qui se passe au 1er tour, quand content vaut NULL ;
             *   - en cas d'echec, renvoie NULL MAIS l'ancien bloc existe
             *     toujours.
             * D'ou la variable "grown" : si on ecrivait directement
             * content = realloc(content, ...) et que ca echouait, on
             * perdrait la seule adresse de l'ancien bloc (fuite memoire).
             */
            char *grown = realloc(content, capacity);
            if (!grown) {
                free(content);      // libere l'ancien bloc
                fclose(file);       // ferme le fichier
                return NULL;        // signale l'echec
            }
            content = grown;        // succes : on garde la nouvelle adresse
        }

        // Copie les got octets de chunk A LA SUITE de ce qu'on a deja.
        // "content + length" = adresse de la premiere case libre :
        // arithmetique de pointeurs, on avance de length cases.
        memcpy(content + length, chunk, got);
        // On a maintenant got octets de plus de remplis.
        length += got;
    }
    fclose(file);   // toujours fermer ce qu'on a ouvert

    if (!content) {
        // Fichier vide : la boucle n'a fait aucun tour, rien n'a ete
        // alloue. On renvoie quand meme une chaine vide "" (1 octet '\0'),
        // pour que l'appelant n'ait pas a gerer ce cas a part.
        // calloc(n, taille) = malloc + met tout a zero.
        content = calloc(1, 1);
    } else {
        // Termine la chaine. La place existe forcement grace au "+ 1" de
        // la ligne cle : length + 1 <= capacity.
        content[length] = '\0';
    }
    return content;
}

/* =========================================================================
 * 3. instance_read : LA FONCTION PRINCIPALE
 * =========================================================================
 *
 *   path       : chemin du fichier a lire
 *   out        : la struct Instance a remplir (fournie par l'appelant)
 *   error      : tampon pour le message d'erreur
 *   error_size : taille de ce tampon
 *
 * Renvoie 0 si tout va bien, -1 sinon.
 *
 * Gestion d'erreur avec "goto cleanup" : la fonction alloue plusieurs
 * choses (content, lines, values, machine_used, out->ops). Quelle que soit
 * l'erreur, on saute a l'etiquette "cleanup" en bas, qui libere tout. Ca
 * evite de recopier la liste des free() a chaque erreur. C'est l'usage
 * normal et accepte de goto en C.
 *
 * Pour que ca marche, chaque pointeur est initialise a NULL au debut :
 * free(NULL) ne fait rien, donc on peut liberer sans se demander ce qui a
 * deja ete alloue ou non.
 */
int instance_read(const char *path, Instance *out, char *error,
                  size_t error_size) {
    // Met toute la struct a zero : name vide, jobs = machines = 0,
    // ops = NULL. Ainsi instance_free(out) est sans danger meme en cas
    // d'erreur precoce.
    memset(out, 0, sizeof *out);

    int status = -1;                    // pessimiste : echec par defaut,
                                        // passe a 0 seulement a la fin
    char **lines = NULL;                // tableau de pointeurs vers les lignes
    long *values = NULL;                // nombres lus sur une ligne de job
    unsigned char *machine_used = NULL; // machine deja vue dans ce job ?

    /* ---- Etape A : charger le fichier ---------------------------------- */
    char *content = read_file(path);
    if (!content) {
        set_error(error, error_size, "Impossible d'ouvrir l'instance: %s",
                  path);
        goto cleanup;
    }

    /* ---- Etape B : decouper en lignes ---------------------------------- */
    /*
     * Idee : remplacer chaque '\n' par '\0'. Le grand texte devient une
     * suite de petites chaines collees, SANS rien copier :
     *
     *   avant : "abz5\n10 10\n4 88 ...\n"
     *   apres : "abz5\0" "10 10\0" "4 88 ...\0"
     *            ^         ^         ^
     *          lines[0]  lines[1]  lines[2]
     *
     * lines[i] n'est qu'un POINTEUR vers l'interieur de content. Donc on
     * ne doit pas liberer content tant qu'on utilise lines.
     */

    // Combien de lignes au maximum ? Nombre de '\n' + 1 (la derniere ligne
    // n'a pas forcement de '\n').
    size_t capacity = 1;
    for (const char *c = content; *c; ++c) {
        if (*c == '\n') ++capacity;
    }
    // Un tableau de "capacity" pointeurs char*.
    // sizeof *lines = taille d'UN element de lines (un char*) : ecrire
    // sizeof *lines plutot que sizeof(char*) reste juste si on change le
    // type de lines un jour.
    lines = malloc(capacity * sizeof *lines);
    if (!lines) {
        set_error(error, error_size, "Memoire insuffisante");
        goto cleanup;
    }

    size_t count = 0;       // nombre de lignes UTILES gardees dans lines
    char *line = content;   // debut de la ligne en cours
    for (char *c = content;; ++c) {
        // On avance jusqu'a une fin de ligne ('\n') ou de texte ('\0').
        if (*c != '\n' && *c != '\0') continue;
        // On retient si c'etait la toute fin AVANT d'ecraser le caractere.
        const int last = *c == '\0';
        *c = '\0';          // coupe : la ligne courante s'arrete ici
        // On ne garde que les lignes utiles (pas vides, pas commentaires).
        if (!is_comment_or_empty(line)) lines[count++] = line;
        if (last) break;    // fin du texte : termine
        line = c + 1;       // la ligne suivante commence juste apres
    }
    if (count == 0) {
        set_error(error, error_size, "Instance vide: %s", path);
        goto cleanup;
    }

    /* ---- Etape C : nom et dimensions ----------------------------------- */
    /*
     * Deux formats existent :
     *   avec nom :  "abz5"  puis  "10 10"
     *   sans nom :  "10 10" directement
     * Test : si la 1re ligne contient exactement 2 entiers, ce sont les
     * dimensions et le nom vient du nom du fichier. Sinon, c'est le nom.
     */
    long dimensions[2];     // [0] = jobs, [1] = machines
    size_t index = 0;       // numero de la prochaine ligne a lire
    if (parse_ints(lines[0], dimensions, 2) == 2) {
        copy_stem(out->name, sizeof out->name, path);
    } else {
        copy_trimmed(out->name, sizeof out->name, lines[0]);
        ++index;            // la ligne 0 etait le nom : on la passe
    }
    if (index >= count) {
        set_error(error, error_size, "Dimensions absentes dans l'instance");
        goto cleanup;
    }
    // lines[index++] : utilise lines[index], PUIS incremente index.
    if (parse_ints(lines[index++], dimensions, 2) != 2   // pas 2 nombres
        || dimensions[0] <= 0 || dimensions[1] <= 0      // pas positifs
        // Garde-fous contre les debordements d'int plus bas :
        //   - on calculera 2 * machines + 1  -> doit tenir dans un int ;
        //   - on calculera jobs * machines   -> idem. On teste
        //     jobs > INT_MAX / machines (division : elle ne deborde pas).
        || dimensions[1] > (INT_MAX - 1) / 2
        || dimensions[0] > INT_MAX / dimensions[1]) {
        set_error(error, error_size, "Dimensions invalides dans l'instance");
        goto cleanup;
    }
    // Les valeurs sont verifiees : la conversion long -> int est sure.
    out->jobs = (int)dimensions[0];
    out->machines = (int)dimensions[1];

    /* ---- Etape D : allocations ----------------------------------------- */
    const int machines = out->machines;     // raccourci
    // Tableau PLAT de jobs * machines operations :
    //   l'operation k du job j est ops[j * machines + k].
    // (size_t) avant la multiplication : le calcul se fait en 64 bits.
    out->ops = malloc((size_t)out->jobs * (size_t)machines * sizeof *out->ops);
    // Une ligne de job contient 2 nombres par machine (machine, duree).
    // +1 case pour que parse_ints detecte "un nombre de trop".
    values = malloc((size_t)(2 * machines + 1) * sizeof *values);
    // Un octet par machine : 0 = pas encore vue dans ce job, 1 = deja vue.
    machine_used = malloc((size_t)machines);
    // On verifie les trois d'un coup : si l'un est NULL, cleanup libere
    // les autres.
    if (!out->ops || !values || !machine_used) {
        set_error(error, error_size, "Memoire insuffisante");
        goto cleanup;
    }

    /* ---- Etape E : lire chaque job ------------------------------------- */
    for (int job = 0; job < out->jobs; ++job) {
        if (index >= count) {
            set_error(error, error_size, "Ligne manquante pour le job %d", job);
            goto cleanup;
        }
        // On attend exactement 2 * machines nombres sur la ligne.
        const int found = parse_ints(lines[index++], values, 2 * machines);
        if (found > 2 * machines) {     // parse_ints a renvoye capacity + 1
            set_error(error, error_size, "Trop de nombres pour le job %d", job);
            goto cleanup;
        }
        if (found != 2 * machines) {    // trop peu, ou -1 (mot invalide)
            set_error(error, error_size, "Operation invalide pour le job %d",
                      job);
            goto cleanup;
        }
        // Remet "aucune machine vue" pour ce nouveau job.
        memset(machine_used, 0, (size_t)machines);
        for (int operation = 0; operation < machines; ++operation) {
            // Les nombres vont par paires : values = [m0, d0, m1, d1, ...]
            const long machine = values[2 * operation];
            const long duration = values[2 * operation + 1];
            if (machine < 0 || machine >= machines   // machine inexistante
                || duration < 0 || duration > INT_MAX) {
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
            // Adresse de la case (job, operation) dans le tableau plat.
            Operation *value =
                &out->ops[(size_t)job * (size_t)machines + (size_t)operation];
            value->machine = (int)machine;   // "->" : champ via un pointeur
            value->duration = (int)duration;
        }
    }

    // Des lignes en trop ne bloquent pas la resolution : on previent
    // seulement, pour ne pas perdre une instance le jour du concours.
    // %zu = format printf pour un size_t.
    if (index < count) {
        fprintf(stderr,
                "Attention: %zu ligne(s) ignoree(s) apres le dernier job\n",
                count - index);
    }
    status = 0;     // on est arrive au bout : succes

cleanup:
    // Atteint dans TOUS les cas : succes (on tombe ici naturellement) ou
    // erreur (via goto). Les tampons de travail ne servent plus : on les
    // libere. free(NULL) ne fait rien, donc pas besoin de tester.
    free(content);
    free(lines);
    free(values);
    free(machine_used);
    // En cas d'echec, on libere aussi ce qu'on avait commence a remplir
    // dans out, pour ne pas laisser une instance a moitie construite.
    // En cas de succes, out->ops est GARDE : il appartient maintenant a
    // l'appelant, qui devra appeler instance_free.
    if (status != 0) instance_free(out);
    return status;
}

/* =========================================================================
 * 4. instance_free : LIBERER
 * ========================================================================= */

/*
 * Chaque malloc doit avoir son free. ops est le seul bloc alloue qui
 * survit a instance_read : c'est donc le seul a liberer ici.
 * On remet le pointeur a NULL : si quelqu'un appelle instance_free deux
 * fois par erreur, le 2e appel fera free(NULL), sans danger.
 */
void instance_free(Instance *instance) {
    free(instance->ops);
    instance->ops = NULL;
}
