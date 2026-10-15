// generator instancji do zad. 1
// uzycie: generator <n> <seed> [plik] [R] [D]      (bez pliku albo "-" -> stdout)
//
// T = suma p + n * sredni setup, czyli mniej wiecej dlugosc harmonogramu
// r ~ U[0, R*T] (domyslnie R = 0.5), d = r + p + U[0, D*T] (domyslnie D = 0.4)
// mniejsze D -> ciasniejsze terminy

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Uzycie: " << argv[0] << " <n> <seed> [plik_wyjsciowy] [R] [D]\n";
        return 1;
    }

    const int n = std::atoi(argv[1]);
    const unsigned seed = static_cast<unsigned>(std::strtoul(argv[2], nullptr, 10));
    const std::string outPath = argc > 3 ? argv[3] : "-";
    const double R = argc > 4 ? std::atof(argv[4]) : 0.5;
    const double D = argc > 5 ? std::atof(argv[5]) : 0.4;

    if (n <= 0) {
        std::cerr << "n musi byc dodatnie\n";
        return 1;
    }

    const int P_MIN = 1, P_MAX = 100;  // p
    const int S_MIN = 0, S_MAX = 30;   // S
    const int W_MIN = 1, W_MAX = 10;   // w

    std::mt19937 rng(seed);
    auto uni = [&](long long lo, long long hi) {
        return std::uniform_int_distribution<long long>(lo, hi)(rng);
    };

    std::vector<long long> p(n), r(n), d(n), w(n);
    std::vector<std::vector<long long>> S(n, std::vector<long long>(n, 0));

    long long sumP = 0;
    for (int j = 0; j < n; ++j) {
        p[j] = uni(P_MIN, P_MAX);
        sumP += p[j];
    }

    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j)
            S[i][j] = (i == j) ? 0 : uni(S_MIN, S_MAX);

    const long long T = sumP + static_cast<long long>(n) * (S_MIN + S_MAX) / 2;
    const long long rMax = static_cast<long long>(R * T);
    const long long slackMax = static_cast<long long>(D * T);

    for (int j = 0; j < n; ++j) {
        r[j] = uni(0, rMax);
        d[j] = r[j] + p[j] + uni(0, slackMax);
        w[j] = uni(W_MIN, W_MAX);
    }

    std::ofstream file;
    if (outPath != "-") {
        file.open(outPath);
        if (!file) {
            std::cerr << "Nie mozna otworzyc pliku: " << outPath << "\n";
            return 1;
        }
    }
    std::ostream& out = (outPath != "-") ? file : std::cout;

    out << n << "\n";
    for (int j = 0; j < n; ++j)
        out << p[j] << " " << r[j] << " " << d[j] << " " << w[j] << "\n";
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            if (j) out << " ";
            out << S[i][j];
        }
        out << "\n";
    }

    return 0;
}
