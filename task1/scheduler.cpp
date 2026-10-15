// Algorytm dla problemu 1 | r_j, S_ij | sum w_j U_j
//
// Uzycie:
//   scheduler <plik_instancji> <plik_wynikowy> [limit_czasu_s]
//
//   limit_czasu_s - limit czasu w sekundach (domyslnie n/10)
//
// Idea (wersja laczona):
//   1. Konstrukcja: heurystyka zachlanna - w kazdym kroku sposrod zadan, ktore jeszcze zdaza na
//      termin, wybieramy zadanie o najlepszym priorytecie; zadania, ktore juz nie zdaza, trafiaja
//      na koniec (metoda najblizszego sasiada wzgledem przezbrojen). Reguly priorytetu: 4 proste
//      (najwczesniejsze zakonczenie, EDD, czas/waga, luz/waga) oraz ATCS (Apparent Tardiness
//      Cost with Setups) na siatce parametrow 6x6.
//   1b. Strojenie konstrukcji: parametry ATCS (k1, k2, kw) losujemy (globalnie albo wokol
//      najlepszych) i zostawiamy najlepsza sekwencje - do TUNE_PCT % limitu lub TUNE_STALL prob
//      bez poprawy.
//   2. Poprawa: symulowane wyzarzanie (jeden cykl chlodzenia na pozostaly czas; temperatura
//      maleje geometrycznie z uplywem czasu), najlepsze rozwiazanie zapamietywane. Ruchy: insert,
//      swap, eject (spoznione na koniec), celowany insert (blisko ostatniej pozycji, gdzie zadanie
//      zdazy) oraz rescue (ciezkie spoznione zadanie na pozycje, gdzie zdazy, ew. z wyrzuceniem
//      lzejszego spoznionego na koniec). Z USE_SA=0: akceptacja ruchow nie pogarszajacych
//      i perturbacja najlepszego przy stagnacji (jak w wersji bazowej).
//
// Funkcja kosztu: para (sum w_j U_j, sum w_j T_j zadan spoznionych) porownywana leksykograficznie.
// Wazone spoznienie rozroznia sekwencje o tej samej karze i "przyciaga" spoznione zadania blisko
// ich terminow, wiec kolejny ruch latwiej zamienia je w zadania na czas.
//
// Szybka ocena ruchu: harmonogram biezacej sekwencji jest zbuforowany (C, kara i spoznienie
// narastajaco), ruch liczymy od pierwszej zmienionej pozycji do osobnego bufora i przerywamy,
// gdy wiadomo, ze jest gorszy, albo gdy harmonogram "dogonil" stary (reszta bez zmian).
//
// Model harmonogramu taki jak w weryfikatorze:
//   start_(1) = r_(1),  start_(k) = max(C_(k-1) + S_(k-1),(k), r_(k)),  C_(k) = start_(k) + p_(k)

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <vector>

using Clock = std::chrono::steady_clock;

// Udzial czasu (%) na strojenie parametrow konstrukcji
#ifndef TUNE_PCT
#define TUNE_PCT 40
#endif
// strojenie konczy sie tez po tylu probach bez poprawy
#ifndef TUNE_STALL
#define TUNE_STALL 300
#endif
// Mieszanka ruchow LS w procentach (reszta do 100 to rescue)
#ifndef P_INSERT
#define P_INSERT 45
#endif
#ifndef P_SWAP
#define P_SWAP 35
#endif
#ifndef P_EJECT
#define P_EJECT 5
#endif
#ifndef P_TARGET
#define P_TARGET 10
#endif

// Symulowane wyzarzanie zamiast akceptacji ruchow nie pogarszajacych + perturbacji
#ifndef USE_SA
#define USE_SA 1
#endif
#ifndef SA_ACC0
#define SA_ACC0 0.1
#endif
#ifndef SA_TEND
#define SA_TEND 0.003
#endif

// Dane zadania - w petli oceny czytane razem, wiec trzymane obok siebie (16 bajtow)
struct Job {
    int32_t r, p, d, w;
};

struct Instance {
    int n = 0;
    std::vector<Job> job;
    std::vector<int32_t> S;          // macierz przezbrojen wierszami: S[i*n + j]
    std::vector<double> logW, logP;  // log w_j, log p_j - przyspieszenie reguly ATCS

    int32_t setup(int i, int j) const { return S[static_cast<size_t>(i) * n + j]; }
    const int32_t* row(int i) const { return &S[static_cast<size_t>(i) * n]; }
};

static bool readInstance(const std::string& path, Instance& inst) {
    std::ifstream in(path);
    if (!in || !(in >> inst.n) || inst.n <= 0) return false;
    const int n = inst.n;
    inst.job.resize(n);
    inst.S.resize(static_cast<size_t>(n) * n);
    // wartosci musza miescic sie w int32 (sumy czasow i kar liczymy w 64 bitach)
    auto rd = [&](int32_t& x) {
        long long v;
        if (!(in >> v) || v < 0 || v > std::numeric_limits<int32_t>::max()) return false;
        x = static_cast<int32_t>(v);
        return true;
    };
    for (int j = 0; j < n; ++j) {
        Job& J = inst.job[j];
        if (!rd(J.p) || !rd(J.r) || !rd(J.d) || !rd(J.w)) return false;
    }
    for (size_t k = 0; k < inst.S.size(); ++k)
        if (!rd(inst.S[k])) return false;
    for (int j = 0; j < n; ++j) {
        inst.logW.push_back(std::log(static_cast<double>(std::max(1, inst.job[j].w))));
        inst.logP.push_back(std::log(static_cast<double>(std::max(1, inst.job[j].p))));
    }
    return true;
}

// Koszt porownywany leksykograficznie: najpierw kara sum w_j U_j, potem wazone spoznienie
struct Cost {
    long long pen, tard;
    bool operator<(const Cost& o) const { return pen != o.pen ? pen < o.pen : tard < o.tard; }
    bool operator<=(const Cost& o) const { return !(o < *this); }
};
static const Cost WORSE{std::numeric_limits<long long>::max(), 0};  // "ruch na pewno gorszy"

// Ocena sekwencji z buforowaniem: C[k], pen[k] (kara narastajaco) i wt[k] (wazone spoznienie
// narastajaco) dla pozycji 0..k. Ruch oceniamy do buforow roboczych nC/nPen/nWt,
// a do glownych kopiujemy dopiero po akceptacji (commit) - odrzucenie nic nie kosztuje.
// Niezmiennik: C/pen/wt zawsze opisuja biezaca sekwencje (z niego korzystaja ruchy celowane).
struct Evaluator {
    const Instance& I;
    std::vector<long long> C, pen, wt;     // harmonogram biezacej sekwencji
    std::vector<long long> nC, nPen, nWt;  // bufor roboczy ocenianego ruchu
    int evalEnd = 0;                       // ostatnia pozycja policzona w buforze roboczym
    bool cutTail = false;                  // czy ocena skonczyla sie wczesniej (dalej bez zmian)

    explicit Evaluator(const Instance& inst)
        : I(inst), C(inst.n), pen(inst.n), wt(inst.n), nC(inst.n), nPen(inst.n), nWt(inst.n) {}

    Cost current() const { return {pen[I.n - 1], wt[I.n - 1]}; }

    // Pelne przeliczenie od pozycji 0 do glownych buforow; zwraca koszt
    Cost eval(const std::vector<int>& seq) {
        const int n = I.n;
        long long t = 0, acc = 0, tard = 0;
        for (int k = 0; k < n; ++k) {
            const int j = seq[k];
            const Job& J = I.job[j];
            const long long setup = k > 0 ? I.setup(seq[k - 1], j) : 0;
            t = std::max(t + setup, static_cast<long long>(J.r)) + J.p;
            if (t > J.d) {
                acc += J.w;
                tard += J.w * (t - J.d);
            }
            C[k] = t;
            pen[k] = acc;
            wt[k] = tard;
        }
        return {acc, tard};
    }

    // Ocena sekwencji seq zmienionej tylko na pozycjach lo..hi (stary harmonogram w C/pen/wt).
    // Zwraca WORSE, gdy koszt na pewno przekroczy prog bound (wczesne odrzucenie);
    // bound = WORSE wylacza odrzucanie (dokladny koszt, np. przy wyborze najlepszego kandydata).
    Cost evalMove(const std::vector<int>& seq, int lo, int hi, const Cost& bound) {
        const int n = I.n;
        const long long curPen = pen[n - 1], curWt = wt[n - 1];
        long long t = lo > 0 ? C[lo - 1] : 0;
        long long acc = lo > 0 ? pen[lo - 1] : 0;
        long long tard = lo > 0 ? wt[lo - 1] : 0;
        const int32_t* Srow = lo > 0 ? I.row(seq[lo - 1]) : nullptr;
        for (int k = lo; k < n; ++k) {
            const int j = seq[k];
            const Job& J = I.job[j];
            const long long setup = Srow ? Srow[j] : 0;
            t = std::max(t + setup, static_cast<long long>(J.r)) + J.p;
            if (t > J.d) {
                acc += J.w;
                tard += J.w * (t - J.d);
                // kara i spoznienie narastajaco nie maleja - koszt na pewno powyzej progu
                if (bound < Cost{acc, tard}) return WORSE;
            }
            nC[k] = t;
            nPen[k] = acc;
            nWt[k] = tard;
            // za hi sekwencja jest stara (takze zadanie na pozycji k, od ktorego zalezy
            // przezbrojenie do k+1); gdy C_k jak dawniej, reszta harmonogramu bez zmian
            if (k > hi) {
                if (t == C[k]) {
                    evalEnd = k;
                    cutTail = true;
                    return {acc + curPen - pen[k], tard + curWt - wt[k]};
                }
                // C_k pozniej niz dawniej: dalsze zadania skoncza sie nie wczesniej niz poprzednio,
                // wiec koszt koncowy >= koszt do k + stary koszt koncowki; gdy to ponad prog - odrzuc
                if (t > C[k] && bound < Cost{acc + curPen - pen[k], tard + curWt - wt[k]}) return WORSE;
            }
            Srow = I.row(j);
        }
        evalEnd = n - 1;
        cutTail = false;
        return {acc, tard};
    }

    // Przyjecie ocenionego ruchu (ostatnie evalMove od pozycji lo): bufor roboczy -> glowny
    void commit(int lo) {
        const int n = I.n;
        const int e = evalEnd;
        std::copy(nC.begin() + lo, nC.begin() + e + 1, C.begin() + lo);
        if (cutTail) {  // dalej harmonogram bez zmian, przesuwamy tylko sumy narastajace
            const long long dPen = nPen[e] - pen[e], dWt = nWt[e] - wt[e];
            for (int k = e + 1; k < n; ++k) {
                pen[k] += dPen;
                wt[k] += dWt;
            }
        }
        std::copy(nPen.begin() + lo, nPen.begin() + e + 1, pen.begin() + lo);
        std::copy(nWt.begin() + lo, nWt.begin() + e + 1, wt.begin() + lo);
    }
};

// Przeniesienie elementu z pozycji a na pozycje b (rownowazne erase + insert)
static inline void moveElem(std::vector<int>& v, int a, int b) {
    if (a < b)
        std::rotate(v.begin() + a, v.begin() + a + 1, v.begin() + b + 1);
    else if (a > b)
        std::rotate(v.begin() + b, v.begin() + a, v.begin() + a + 1);
}

// Parametry reguly priorytetu (konstrukcji zachlannej)
struct Rule {
    int variant;    // 0..3 - reguly proste, 4 - ATCS
    double k1, k2;  // parametry ATCS (skala luzu i skala przezbrojenia)
    double kw;      // wykladnik wagi w ATCS (w^kw / p)
};

// Heurystyka zachlanna. W kazdym kroku sposrod zadan, ktore jeszcze zdaza, wybieramy to
// o najmniejszym kluczu (regula R).
static std::vector<int> greedy(const Instance& I, const Rule& R, double avgP, double avgS) {
    const int n = I.n;
    std::vector<int> seq;
    std::vector<char> used(n, 0);
    long long t = 0;
    int last = -1;
    const double invSlack = 1.0 / (R.k1 * avgP), invLost = 1.0 / (R.k2 * avgS);

    // alive - zadania nieuzyte, ktore moga jeszcze zdazyc (t rosnie, wiec raz spoznione
    // nawet bez przezbrojenia zadanie zostaje spoznione - usuwamy je z listy na stale)
    std::vector<int> alive(n);
    for (int j = 0; j < n; ++j) alive[j] = j;

    while (!alive.empty()) {
        int best = -1;
        double bestKey = std::numeric_limits<double>::max();
        size_t keep = 0;
        for (size_t q = 0; q < alive.size(); ++q) {
            const int j = alive[q];
            const Job& J = I.job[j];
            if (std::max(t, static_cast<long long>(J.r)) + J.p > J.d) continue;  // nie zdazy nigdy
            alive[keep++] = j;
            const long long setup = last >= 0 ? I.setup(last, j) : 0;
            const long long start = std::max(t + setup, static_cast<long long>(J.r));
            const long long C = start + J.p;
            if (C > J.d) continue;  // teraz nie zdazy
            double key;
            switch (R.variant) {
                case 0: key = static_cast<double>(C); break;                    // najwczesniej skonczone
                case 1: key = static_cast<double>(J.d); break;                  // EDD
                case 2: key = static_cast<double>(C - t) / J.w; break;          // zuzyty czas / waga
                case 3: key = static_cast<double>(J.d - C) / J.w; break;        // luz / waga
                default: {
                    // ATCS: w^kw/p * exp(-luz/(k1*avgP)) * exp(-strata/(k2*avgS)), gdzie strata to
                    // przezbrojenie + przestoj w oczekiwaniu na r_j. Minimalizujemy -log(indeksu).
                    const double slack = static_cast<double>(J.d - C);
                    const double lost = static_cast<double>(start - t);
                    key = -(R.kw * I.logW[j] - I.logP[j] - slack * invSlack - lost * invLost);
                }
            }
            // remis rozstrzyga mniejszy numer zadania (jak w regulach bazowych)
            if (key < bestKey || (key == bestKey && j < best)) {
                bestKey = key;
                best = j;
            }
        }
        alive.resize(keep);
        if (best < 0) break;  // zadne pozostale zadanie nie zdazy - reszta i tak bedzie spozniona
        const long long setup = last >= 0 ? I.setup(last, best) : 0;
        t = std::max(t + setup, static_cast<long long>(I.job[best].r)) + I.job[best].p;
        used[best] = 1;
        seq.push_back(best);
        last = best;
        alive.erase(std::find(alive.begin(), alive.end(), best));
    }

    // Spoznione zadania na koniec, metoda najblizszego sasiada wzgledem przezbrojen
    while (static_cast<int>(seq.size()) < n) {
        int best = -1;
        for (int j = 0; j < n; ++j)
            if (!used[j] && (best < 0 || (last >= 0 && I.setup(last, j) < I.setup(last, best)))) best = j;
        used[best] = 1;
        seq.push_back(best);
        last = best;
    }
    return seq;
}

// Losowe przeszukiwanie lokalne od sekwencji best do deadline. Akceptacja: wyzarzanie (USE_SA=1)
// albo ruchy nie pogarszajace + perturbacja najlepszego przy stagnacji (USE_SA=0).
// Zwraca liczbe iteracji.
//   Ruchy (proporcje P_* w procentach):
//   - insert: przeniesienie losowego zadania na losowa pozycje,
//   - swap: zamiana dwoch losowych zadan,
//   - eject: zadanie spoznione na koniec sekwencji (jego kara jest stala, a zwalnia miejsce),
//   - celowany insert: przeniesienie w okolice ostatniej pozycji, na ktorej zadanie jeszcze zdazy,
//   - rescue: ciezkie zadanie spoznione na pozycje, gdzie zdazy; jesli to pogarsza wynik,
//     wyrzucamy na koniec pierwsze zadanie spoznione o mniejszej wadze za nim (ruch zlozony).
static long long localSearch(const Instance& I, Evaluator& ev, std::vector<int>& best, Cost& bestCost,
                             Clock::time_point deadline, std::mt19937& rng) {
    const int n = I.n;
    if (n < 2) return 0;
    std::uniform_int_distribution<int> pos(0, n - 1);
    const int MOVE_INSERT = P_INSERT, MOVE_SWAP = MOVE_INSERT + P_SWAP, MOVE_EJECT = MOVE_SWAP + P_EJECT,
              MOVE_TARGET = MOVE_EJECT + P_TARGET;  // reszta - rescue
    const int RESCUE_SAMPLES = 3;     // z ilu losowych spoznionych wybieramy najciezsze
    const int RESCUE_CANDIDATES = 2;  // ile pozycji (najpozniejszych, gdzie zdazy) oceniamy

#if USE_SA
    // Wyzarzanie: pogorszenie kary o delta przyjmujemy z prawdop. exp(-delta/T). Realizacja progiem:
    // losujemy u i dopuszczamy kare do cur + floor(-T ln u) (dzieki temu dziala wczesne odrzucanie).
    // T0: pogorszenie o srednia wage przyjmowane z prawdop. SA_ACC0; T maleje geometrycznie
    // z uplywem czasu do T0 * SA_TEND na koncu limitu.
    double avgW = 0;
    for (int j = 0; j < n; ++j) avgW += I.job[j].w;
    avgW /= n;
    const double T0 = avgW / std::log(1.0 / SA_ACC0), Tend = T0 * SA_TEND;
    const auto saStart = Clock::now();
    const double saTotal = std::max(1e-9, std::chrono::duration<double>(deadline - saStart).count());
    double T = T0;
    std::uniform_real_distribution<double> u01(0.0, 1.0);
#endif
    std::vector<int> cur = best;
    Cost curCost = ev.eval(cur);
    long long sinceImprove = 0;
    const long long stagnation = 50LL * n;

    // Czy zadanie na pozycji k jest spoznione (wg harmonogramu biezacej sekwencji)
    auto isLate = [&](int k) { return ev.C[k] > I.job[cur[k]].d; };
    // Losowa pozycja zadania spoznionego (kilka prob), -1 gdy nie znaleziono
    auto randomLate = [&]() {
        for (int t = 0; t < 8; ++t) {
            const int k = pos(rng);
            if (isLate(k)) return k;
        }
        return -1;
    };
    // Pierwsza pozycja k z C[k] > x (C jest niemalejace)
    auto timeToPos = [&](long long x) {
        return static_cast<int>(std::upper_bound(ev.C.begin(), ev.C.end(), x) - ev.C.begin());
    };
    // Ocena ruchu, ktory zmienil pozycje lo..hi, i akceptacja (true) albo odrzucenie (false).
    // Po odrzuceniu wywolujacy musi przywrocic cur; bufory ewaluatora sa nadal aktualne.
    Cost bound = curCost;  // prog akceptacji w biezacej iteracji
    auto tryAccept = [&](int lo, int hi) {
        const Cost c = ev.evalMove(cur, lo, hi, bound);
        if (!(c <= bound)) return false;
        ev.commit(lo);
        if (c < curCost) sinceImprove = 0;
        curCost = c;
        if (c < bestCost) {
            bestCost = c;
            best = cur;
        }
        return true;
    };
    // Przeniesienie jednego zadania z a na b z akceptacja lub cofnieciem
    auto tryInsert = [&](int a, int b) {
        moveElem(cur, a, b);
        if (!tryAccept(std::min(a, b), std::max(a, b))) moveElem(cur, b, a);
    };

    long long it = 0;
    for (;; ++it) {
        if ((it & 255) == 0) {
            const auto now = Clock::now();
            if (now >= deadline) break;
#if USE_SA
            const double frac = std::chrono::duration<double>(now - saStart).count() / saTotal;
            T = T0 * std::pow(Tend / T0, std::min(1.0, frac));
#endif
        }
#if USE_SA
        bound = {curCost.pen + static_cast<long long>(-T * std::log(1.0 - u01(rng))), curCost.tard};
#else
        bound = curCost;
#endif
        const int m = static_cast<int>(rng() % 100);

        if (m < MOVE_INSERT) {  // insert losowy
            const int a = pos(rng), b = pos(rng);
            if (a != b) tryInsert(a, b);
        } else if (m < MOVE_SWAP) {  // swap
            const int a = pos(rng), b = pos(rng);
            if (a != b) {
                std::swap(cur[a], cur[b]);
                if (!tryAccept(std::min(a, b), std::max(a, b))) std::swap(cur[a], cur[b]);
            }
        } else if (m < MOVE_EJECT) {  // eject: zadanie spoznione na koniec
            const int a = randomLate();
            if (a >= 0 && a < n - 1) tryInsert(a, n - 1);
        } else if (m < MOVE_TARGET) {  // celowany insert
            const int a = pos(rng);
            const Job& J = I.job[cur[a]];
            // ostatnia pozycja, przed ktora jest jeszcze czas na wykonanie zadania, minus losowy odstep
            const int tgt = timeToPos(static_cast<long long>(J.d) - J.p) - static_cast<int>(rng() % 8);
            int b = std::max(0, std::min(n - 1, tgt));
            if (b > a) b = std::max(a + 1, b - 1);  // po wyjeciu z a dalsze pozycje sie przesuwaja
            if (a != b) tryInsert(a, b);
        } else {  // rescue
            int a = -1;
            for (int t = 0; t < RESCUE_SAMPLES; ++t) {  // najciezsze z kilku losowych spoznionych
                const int k = randomLate();
                if (k >= 0 && (a < 0 || I.job[cur[k]].w > I.job[cur[a]].w)) a = k;
            }
            if (a > 0) {
                const int j = cur[a];
                const Job& J = I.job[j];
                // Kandydaci: najpozniejsze pozycje k<a, na ktorych j lokalnie zdazy
                // (dla k<a wartosci C sprzed ruchu sa aktualne). Wybieramy najlepsza z nich.
                int bestB = -1;
                Cost bestC = WORSE;
                int tried = 0;
                for (int k = std::min(a, timeToPos(J.d)); k >= 0 && tried < RESCUE_CANDIDATES; --k) {
                    const long long prevC = k > 0 ? ev.C[k - 1] : 0;
                    const long long st = k > 0 ? I.setup(cur[k - 1], j) : 0;
                    if (std::max(prevC + st, static_cast<long long>(J.r)) + J.p > J.d) continue;
                    ++tried;
                    moveElem(cur, a, k);
                    const Cost c = ev.evalMove(cur, k, a, WORSE);
                    if (c < bestC) {
                        bestC = c;
                        bestB = k;
                    }
                    moveElem(cur, k, a);
                }
                if (bestB >= 0) {
                    moveElem(cur, a, bestB);
                    if (!tryAccept(bestB, a)) {
                        // naprawa: pierwsze spoznione (w nowym harmonogramie) zadanie o mniejszej
                        // wadze za a -> na koniec; ocena ruchu zlozonego od bestB do konca
                        ev.evalMove(cur, bestB, n - 1, WORSE);  // pelny nowy harmonogram w nC
                        int e = -1;
                        for (int k = bestB + 1; k < n - 1; ++k)
                            if (ev.nC[k] > I.job[cur[k]].d && I.job[cur[k]].w < J.w) {
                                e = k;
                                break;
                            }
                        if (e >= 0) {
                            moveElem(cur, e, n - 1);
                            if (!tryAccept(bestB, n - 1)) moveElem(cur, n - 1, e);
                            else e = -2;  // przyjete
                        }
                        if (e != -2) moveElem(cur, bestB, a);  // cofnij przeniesienie j
                    }
                }
            }
        }

        // Stagnacja: wroc do najlepszego i zaburz je kilkoma losowymi przeniesieniami
        if (!USE_SA && ++sinceImprove > stagnation) {
            cur = best;
            const int kicks = 2 + static_cast<int>(rng() % 4);
            for (int k = 0; k < kicks; ++k) moveElem(cur, pos(rng), pos(rng));
            curCost = ev.eval(cur);
            sinceImprove = 0;
        }
    }
    return it;
}

int main(int argc, char* argv[]) {
    const auto startTime = Clock::now();

    if (argc < 3) {
        std::cerr << "Uzycie: " << argv[0] << " <plik_instancji> <plik_wynikowy> [limit_czasu_s]\n";
        return 1;
    }
    Instance I;
    if (!readInstance(argv[1], I)) {
        std::cerr << "Blad odczytu instancji: " << argv[1] << "\n";
        return 1;
    }
    const int n = I.n;
    const double limit = argc > 3 ? std::atof(argv[3]) : n / 10.0;
    // zapas na zapis wyniku i uruchomienie procesu
    const auto deadline = startTime + std::chrono::duration_cast<Clock::duration>(
                                          std::chrono::duration<double>(std::max(0.0, limit * 0.95 - 0.05)));

    Evaluator ev(I);
    // ziarno z SCHED_SEED (benchmark), domyslnie 12345
    const char* seedEnv = std::getenv("SCHED_SEED");
    std::mt19937 rng(seedEnv ? static_cast<unsigned>(std::strtoul(seedEnv, nullptr, 10)) : 12345u);

    // srednie p i S - skale dla ATCS
    double avgP = 0, avgS = 0;
    for (int j = 0; j < n; ++j) avgP += I.job[j].p;
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j)
            if (i != j) avgS += I.setup(i, j);
    avgP = std::max(1.0, avgP / n);
    avgS = n > 1 ? std::max(1.0, avgS / (static_cast<double>(n) * (n - 1))) : 1.0;

    // 1. Konstrukcja - najlepszy z wariantow deterministycznych (4 proste + siatka ATCS)
    std::vector<Rule> rules;
    for (int v = 0; v < 4; ++v) rules.push_back({v, 1, 1, 1});
    for (double k1 : {1.0, 2.0, 4.0, 8.0, 16.0, 64.0})
        for (double k2 : {0.05, 0.1, 0.2, 0.4, 0.8, 1.6}) rules.push_back({4, k1, k2, 1});
    std::vector<int> best;
    Cost bestCost = WORSE;
    Rule bestRule = rules[0];
    for (const Rule& R : rules) {
        std::vector<int> s = greedy(I, R, avgP, avgS);
        const Cost c = ev.eval(s);
        if (c < bestCost) {
            bestCost = c;
            best = s;
            bestRule = R;
        }
    }

    // 1b. Strojenie konstrukcji: losowe parametry ATCS (globalnie lub wokol najlepszych),
    //     przez TUNE_PCT % czasu albo do TUNE_STALL prob bez poprawy
    const auto tuneEnd = startTime + (deadline - startTime) * TUNE_PCT / 100;
    std::uniform_real_distribution<double> u01(0.0, 1.0);
    std::normal_distribution<double> nrm(0.0, 1.0);
    long long sinceBetter = 0;
    while (TUNE_PCT > 0 && sinceBetter < TUNE_STALL && Clock::now() < tuneEnd) {
        ++sinceBetter;
        Rule R{4, 0, 0, 1};
        if (bestRule.variant == 4 && (rng() & 1)) {
            // lokalnie: zaburzenie najlepszych dotad parametrow
            R = bestRule;
            R.k1 *= std::exp(0.4 * nrm(rng));
            R.k2 *= std::exp(0.4 * nrm(rng));
            R.kw = std::max(0.2, R.kw + 0.15 * nrm(rng));
        } else {
            // globalnie: log-jednostajnie k1 w [0.5, 128], k2 w [0.02, 4], kw w [0.5, 2]
            R.k1 = std::exp(std::log(0.5) + u01(rng) * std::log(256.0));
            R.k2 = std::exp(std::log(0.02) + u01(rng) * std::log(200.0));
            R.kw = 0.5 + 1.5 * u01(rng);
        }
        std::vector<int> s = greedy(I, R, avgP, avgS);
        const Cost c = ev.eval(s);
        if (c < bestCost) {
            bestCost = c;
            best = s;
            bestRule = R;
            sinceBetter = 0;
        }
    }

    // 2. Przeszukiwanie lokalne od najlepszego startu do konca limitu
    localSearch(I, ev, best, bestCost, deadline, rng);

    // Zapis wyniku
    const Cost fin = ev.eval(best);
    std::ofstream out(argv[2]);
    if (!out) {
        std::cerr << "Nie mozna zapisac wyniku: " << argv[2] << "\n";
        return 1;
    }
    out << fin.pen << "\n";
    for (int k = 0; k < n; ++k) out << (k ? " " : "") << best[k] + 1;
    out << "\n";
    return 0;
}
