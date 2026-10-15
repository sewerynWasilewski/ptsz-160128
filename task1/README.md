# Zadanie 1A — 1 | r<sub>j</sub>, S<sub>ij</sub> | ΣwjUj

Jedna maszyna, zadania z czasami gotowości r<sub>j</sub>, terminami d<sub>j</sub>, wagami w<sub>j</sub> i czasami
przezbrojeń S<sub>ij</sub> zależnymi od sekwencji. Kryterium: suma wag zadań spóźnionych
(U<sub>j</sub> = 1 gdy C<sub>j</sub> > d<sub>j</sub>).

## Budowanie i uruchomienie

```sh
g++ -O2 -std=c++17 -o 160128 scheduler.cpp      # albo CMake z katalogu głównego
./160128 in_166123_50.txt out_166123_160128_50.txt 5
```

Parametry: plik z instancją, plik wynikowy, limit czasu w sekundach (opcjonalny, domyślnie n/10).
Program jest jednowątkowy, nic nie wypisuje na standardowe wyjście, kończy się z kodem 0;
przy błędzie odczytu/zapisu wypisuje komunikat na stderr i kończy się z kodem 1.

## Formaty plików

Wejście:

```
n
p1 r1 d1 w1
…
pn rn dn wn
S11 … S1n
…
Sn1 … Snn
```

Wyjście:

```
ΣwjUj
J(1) J(2) … J(n)
```

Zadania numerowane od 1 w kolejności z pliku wejściowego.

## Model harmonogramu

Sekwencja wyznacza harmonogram:

```
start(1) = r(1)                                  (brak przezbrojenia przed pierwszym zadaniem)
start(k) = max( C(k-1) + S(k-1),(k) ,  r(k) )    (przezbrojenie może odbyć się przed r(k))
C(k)     = start(k) + p(k)
```

## Algorytm (w skrócie)

1. Konstrukcja zachłanna z kilkoma regułami priorytetu (w tym ATCS ze strojonymi parametrami);
   zadania, które nie zdążą, trafiają na koniec sekwencji.
2. Symulowane wyżarzanie z ruchami insert, swap oraz ruchami dopasowanymi do kryterium
   (przenoszenie zadań spóźnionych, wstawianie zadania tam, gdzie zdąży).
3. Rozwiązania porównywane leksykograficznie: (ΣwjUj, suma ważonych spóźnień).
