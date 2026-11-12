# jsp_pso en C

Portage en C11 du solveur PSO multi-essaims pour le job shop
(`../optimisation-combinatoire`, en C++17). Memes options, meme format de
sortie JSON, memes decodeurs (verifies identiques au C++ sur 12 000 vecteurs
de cles).

## Compilation

GCC ou Clang requis (pthreads + `<stdatomic.h>`) ; sous Windows, MSYS2
UCRT64 / MinGW-w64. MSVC n'est pas supporte.

```sh
# Sans outil de build
gcc -O3 -std=c11 -Wall -Wextra -Isrc -pthread -o build/jsp_pso src/main.c \
    src/instance.c src/schedule.c src/pso.c src/platform.c -lm

# Ou avec make / CMake
make && make test
cmake -B build && cmake --build build && ctest --test-dir build
```

## Utilisation

```sh
build/jsp_pso instances/abz5 --time 55 --out resultats
```

Options : `--time S` (0 = sans limite), `--target M`, `--seed N`,
`--threads N`, `--particles N`, `--decoder gt|semi`, `--delta D`,
`--topology ring|global|cl|mixed`, `--neighbors K`, `--restart N`,
`--migrate N`, `--vmax V`, `--no-decreasing-inertia`, `--log FICHIER`.

## Correspondance avec la version C++

| C++ | C |
|---|---|
| `std::vector<std::vector<Operation>>` | tableau plat `ops[job * machines + k]` |
| exceptions | codes de retour (0 / -1) + message dans un tampon |
| `std::string` decoder / topology | `enum Decoder`, `enum Topology` |
| lambdas du worker | fonctions `static` prenant un `Worker *` |
| `thread_local` du decodeur | `DecodeWork` explicite, un par thread |
| `std::mt19937_64` | xoshiro256** (`rng.h`) |
| `std::stable_sort` | `qsort` departage par indice |
| `std::thread`, `std::mutex`, `std::atomic` | pthreads, `<stdatomic.h>` |
| `std::chrono`, `std::filesystem` | `platform.c` |

Le generateur aleatoire differe : a graine egale, les trajectoires ne sont
pas les memes qu'en C++, seule la qualite statistique est comparable.
