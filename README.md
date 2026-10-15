# PTSZ — algorytmy szeregowania (160128)

Programy do zadań laboratoryjnych z przedmiotu *Praktyka i teoria szeregowania zadań*.
Każde zadanie jest w osobnym katalogu.

| zadanie | problem | katalog |
|---|---|---|
| 1A | 1 \| r<sub>j</sub>, S<sub>ij</sub> \| ΣwjUj | [`task1/`](task1/README.md) |

## Budowanie

Wymagany kompilator C++17 (g++, clang++ lub MSVC) i CMake ≥ 3.16:

```sh
cmake -S . -B build && cmake --build build --config Release
```

Pliki wykonywalne trafiają do `build/<zadanie>/` i nazywają się numerem indeksu, np. `build/task1/160128`
(na Windows `160128.exe`; przy MSVC w podkatalogu `Release/`).

Bez CMake — jedno polecenie na zadanie, np.:

```sh
g++ -O2 -std=c++17 -o 160128 task1/scheduler.cpp
```

## Uruchamianie

Zgodnie z ustaleniami grupy:

```sh
160128 <plik_instancji> <plik_wynikowy> [limit_czasu_s]
```

np. `./160128 in_166123_50.txt out_166123_160128_50.txt 5`. Limit czasu domyślnie n/10 s;
program kończy obliczenia przy ok. 95% limitu.
