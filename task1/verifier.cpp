// weryfikator rozwiazan do zad. 1
// uzycie: verifier <instancja> <rozwiazanie> [-v]      (-v wypisuje caly harmonogram)
//
// rozwiazanie to sama sekwencja, harmonogram liczony z niej:
//   start(1) = r(1), dalej start(k) = max(C(k-1) + S, r(k)), C(k) = start(k) + p(k)
// wiec r_j i brak nakladania sa spelnione z definicji - sprawdzamy permutacje i kryterium.
// kod wyjscia: 0 ok, 1 blad odczytu/argumentow, 2 zle rozwiazanie

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

struct Instance {
    int n = 0;
    std::vector<long long> p, r, d, w;
    std::vector<std::vector<long long>> S;
};

static bool readInstance(const std::string& path, Instance& inst) {
    std::ifstream in(path);
    if (!in) {
        std::cerr << "Nie mozna otworzyc instancji: " << path << "\n";
        return false;
    }
    if (!(in >> inst.n) || inst.n <= 0) {
        std::cerr << "Niepoprawne n w instancji\n";
        return false;
    }
    const int n = inst.n;
    inst.p.resize(n);
    inst.r.resize(n);
    inst.d.resize(n);
    inst.w.resize(n);
    inst.S.assign(n, std::vector<long long>(n));
    for (int j = 0; j < n; ++j) {
        if (!(in >> inst.p[j] >> inst.r[j] >> inst.d[j] >> inst.w[j])) {
            std::cerr << "Blad odczytu danych zadania " << j + 1 << "\n";
            return false;
        }
    }
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j)
            if (!(in >> inst.S[i][j])) {
                std::cerr << "Blad odczytu macierzy przezbrojen (wiersz " << i + 1 << ")\n";
                return false;
            }
    return true;
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Uzycie: " << argv[0] << " <plik_instancji> <plik_rozwiazania> [-v]\n";
        return 1;
    }
    const bool verbose = argc > 3 && std::string(argv[3]) == "-v";

    Instance inst;
    if (!readInstance(argv[1], inst)) return 1;
    const int n = inst.n;

    std::ifstream sol(argv[2], std::ios::binary);
    if (!sol) {
        std::cerr << "Nie mozna otworzyc rozwiazania: " << argv[2] << "\n";
        return 1;
    }
    const std::string content((std::istreambuf_iterator<char>(sol)), std::istreambuf_iterator<char>());

    // 2 linie: kryterium i sekwencja, puste linie na koncu ignorujemy
    std::vector<std::string> lines;
    {
        std::istringstream ss(content);
        std::string line;
        while (std::getline(ss, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines.push_back(line);
        }
        while (!lines.empty() && lines.back().find_first_not_of(" \t") == std::string::npos)
            lines.pop_back();
    }
    if (lines.size() != 2) {
        std::cout << "BLAD: niepoprawny format - oczekiwano 2 linii (kryterium, sekwencja), jest "
                  << lines.size() << "\n";
        return 2;
    }
    if (content.back() != '\n')
        std::cout << "OSTRZEZENIE: ostatnia linia nie jest zakonczona znakiem konca linii\n";

    auto parseInts = [](const std::string& line, std::vector<long long>& out, std::string& bad) {
        std::istringstream ls(line);
        std::string tok;
        while (ls >> tok) {
            try {
                size_t pos;
                long long v = std::stoll(tok, &pos);
                if (pos != tok.size()) throw std::invalid_argument(tok);
                out.push_back(v);
            } catch (...) {
                bad = tok;
                return false;
            }
        }
        return true;
    };

    std::vector<long long> first, seqRaw;
    std::string bad;
    if (!parseInts(lines[0], first, bad) || first.size() != 1) {
        std::cout << "BLAD: pierwsza linia musi zawierac dokladnie jedna liczbe calkowita (kryterium)\n";
        return 2;
    }
    const long long declared = first[0];
    if (!parseInts(lines[1], seqRaw, bad)) {
        std::cout << "BLAD: niepoprawny token w sekwencji: \"" << bad << "\"\n";
        return 2;
    }
    std::vector<int> seq;
    for (long long v : seqRaw)
        seq.push_back(v < 1 || v > n ? -1 : static_cast<int>(v));

    bool ok = true;
    if (static_cast<int>(seq.size()) != n) {
        std::cout << "BLAD: sekwencja ma " << seq.size() << " elementow, oczekiwano " << n << "\n";
        ok = false;
    }

    std::vector<int> seen(n + 1, 0);
    for (size_t k = 0; k < seq.size(); ++k) {
        const int j = seq[k];
        if (j < 1) {
            std::cout << "BLAD: numer zadania poza zakresem 1.." << n << ": " << seqRaw[k] << "\n";
            ok = false;
        } else if (++seen[j] == 2) {
            std::cout << "BLAD: zadanie " << j << " wystepuje wiecej niz raz\n";
            ok = false;
        }
    }
    for (int j = 1; j <= n; ++j)
        if (!seen[j]) {
            std::cout << "BLAD: brak zadania " << j << " w sekwencji\n";
            ok = false;
        }
    if (!ok) return 2;

    // liczymy harmonogram tak samo jak w algorytmie
    long long t = 0, total = 0;
    int lateCount = 0;
    if (verbose)
        std::cout << std::setw(5) << "poz" << std::setw(7) << "J" << std::setw(10) << "setup"
                  << std::setw(10) << "start" << std::setw(10) << "C" << std::setw(10) << "d"
                  << std::setw(5) << "U" << std::setw(6) << "w" << "\n";
    for (int k = 0; k < n; ++k) {
        const int j = seq[k] - 1;
        const long long setup = k ? inst.S[seq[k - 1] - 1][j] : 0;
        const long long start = std::max(t + setup, inst.r[j]);
        const long long C = start + inst.p[j];
        const int U = C > inst.d[j] ? 1 : 0;
        total += U * inst.w[j];
        lateCount += U;
        t = C;
        if (verbose)
            std::cout << std::setw(5) << k + 1 << std::setw(7) << j + 1 << std::setw(10) << setup
                      << std::setw(10) << start << std::setw(10) << C << std::setw(10) << inst.d[j]
                      << std::setw(5) << U << std::setw(6) << inst.w[j] << "\n";
    }

    std::cout << "Obliczona wartosc kryterium: " << total << " (spoznione zadania: " << lateCount
              << "/" << n << ", Cmax = " << t << ")\n";
    std::cout << "Zadeklarowana wartosc:       " << declared << "\n";

    if (declared != total) {
        std::cout << "BLAD: zadeklarowana wartosc kryterium nie zgadza sie z obliczona\n";
        return 2;
    }
    std::cout << "OK\n";
    return 0;
}
