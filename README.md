# PTSZ — 160128

Programy na laboratoria z Praktyki i teorii szeregowania zadań. Każde zadanie w osobnym katalogu.

| zadanie | problem | katalog |
|---|---|---|
| 1A | 1 \| r<sub>j</sub>, S<sub>ij</sub> \| ΣwjUj | [`task1/`](task1/README.md) |

## Budowanie

Potrzebny kompilator z C++17 i CMake 3.16+:

```sh
cmake -S . -B build && cmake --build build --config Release
```

albo `python3 task1/run.py build`. Pliki wykonywalne lądują w `build/<zadanie>/`
(przy MSVC w podkatalogu `Release/`). Bez CMake wystarczy np.:

```sh
g++ -O2 -std=c++17 -o 160128 task1/scheduler.cpp
```

## Uruchamianie algorytmu

```sh
160128 <instancja> <plik_wynikowy> [limit_s]
```

np. `./160128 in_166123_50.txt out_166123_160128_50.txt 5`. Bez limitu przyjmowane jest n/10 s.
