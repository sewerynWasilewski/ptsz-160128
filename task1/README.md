# Zadanie 1A — 1 | r<sub>j</sub>, S<sub>ij</sub> | ΣwjUj

Jedna maszyna, zadania z czasami gotowości r<sub>j</sub>, terminami d<sub>j</sub>, wagami w<sub>j</sub>
i przezbrojeniami S<sub>ij</sub> zależnymi od kolejności. Minimalizujemy sumę wag zadań spóźnionych.

## Pliki

- `scheduler.cpp` — algorytm (`build/task1/160128`)
- `generator.cpp` — generator instancji (`build/task1/generator`)
- `verifier.cpp` — weryfikator rozwiązań (`build/task1/verifier`)
- `run.py` — budowanie i uruchamianie algorytmu na wielu instancjach z pomiarem czasu

## Użycie

```sh
./build/task1/160128 in_166123_50.txt out_166123_160128_50.txt 5
./build/task1/generator 50 50 in_160128_50.txt          # n, seed, plik [R] [D]
./build/task1/verifier in_166123_50.txt out_166123_160128_50.txt [-v]
```

Algorytm nic nie wypisuje, przy błędzie odczytu/zapisu kończy się kodem 1.
Weryfikator zwraca 0 gdy rozwiązanie jest poprawne, 2 gdy nie, 1 przy błędzie odczytu.

`run.py` szuka instancji w `task1/instances/` (katalog nie jest w repo, trzeba wrzucić pliki samemu),
wyniki zapisuje do `task1/results/`. Inne katalogi: `-i/--instances` i `-r/--results`
albo na stałe przez zmienne `PTSZ_INSTANCES` i `PTSZ_RESULTS`
(np. `export PTSZ_INSTANCES=~/ptsz/instancje` w `~/.zshrc`).

```sh
python3 task1/run.py run                          # wszystkie instancje
python3 task1/run.py run --id 166123              # jednej osoby
python3 task1/run.py run --id 166123 --n 50 500   # wybrane rozmiary
python3 task1/run.py run --limit 1 --csv wyniki.csv
```

Każdy wynik jest od razu sprawdzany weryfikatorem; przekroczony limit pokazuje się jako `CZAS =X`.

Programy innych osób (plik `.cpp` jest kompilowany, `.py` i `.jar` idą przez interpreter,
inny plik jest uruchamiany wprost; indeks brany z nazwy pliku):

```sh
python3 task1/run.py external 155347.cpp
python3 task1/run.py external 155347.py --id 160128
```

Sprawdzenie gotowych plików `out_*.txt` z `task1/results/` (albo `--results <katalog>`):

```sh
python3 task1/run.py verify
python3 task1/run.py verify --prog-id 155347
```

Do wklejania w arkusz: `--only wynik` albo `--only czas` (tylko `run`/`external`) wypisuje samą kolumnę,
po jednej linii na instancję w kolejności autor → n. Błędy są wtedy wpisane jako `BŁĄD`,
a opis idzie na stderr. `--comma` daje przecinek dziesiętny w czasach. Przy weryfikacji rozwiązań
startowych (w pliku zadeklarowane 0) `verify --only wynik --computed` wypisuje policzoną wartość.

## Formaty

Wejście: `n`, potem n linii `p r d w`, potem macierz S (n × n).
Wyjście: w pierwszej linii ΣwjUj, w drugiej kolejność zadań (numeracja od 1).

## Harmonogram z sekwencji

```
start(1) = r(1)
start(k) = max(C(k-1) + S(k-1),(k), r(k))
C(k)     = start(k) + p(k)
```

Przed pierwszym zadaniem nie ma przezbrojenia, przezbrojenie może się odbyć zanim pojazd będzie gotowy.

## Algorytm

1. Konstrukcja zachłanna — kilka reguł priorytetu, w tym ATCS ze strojonymi parametrami.
   Zadania, które nie zdążą, idą na koniec.
2. Symulowane wyżarzanie do końca limitu: insert, swap i ruchy pod to kryterium
   (spóźnione na koniec, wstawianie zadania tam, gdzie zdąży).
3. Przy równej karze lepsze jest rozwiązanie z mniejszą sumą ważonych spóźnień.
