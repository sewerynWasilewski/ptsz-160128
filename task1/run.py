#!/usr/bin/env python3
"""Budowanie, odpalanie algorytmu i weryfikacja wynikow dla zad. 1.

  python3 task1/run.py build
  python3 task1/run.py run                          # wszystko z task1/instances
  python3 task1/run.py run --id 160128 --n 50 500
  python3 task1/run.py external 155347.cpp --only czas      # cudzy .cpp / .py / .jar / plik wykonywalny
  python3 task1/run.py verify                       # wszystkie out_*.txt z task1/results
  python3 task1/run.py verify --prog-id 155347 --only wynik

Czas to czas rzeczywisty procesu. Limit domyslnie n/10 s, po przekroczeniu
"CZAS =X", a po limit + --kill-after s proces jest ubijany.

--only wynik|czas wypisuje sama kolumne (jedna linia na instancje, kolejnosc autor -> n)
do wklejenia w arkusz; bledy ida wtedy jako "BŁĄD", a szczegoly na stderr.
"""
import argparse
import csv
import os
import re
import shlex
import subprocess
import sys
import time
from pathlib import Path

TASK_DIR = Path(__file__).resolve().parent
ROOT = TASK_DIR.parent
BUILD_DIR = ROOT / "build"
EXE = ".exe" if os.name == "nt" else ""
DEFAULT_PROGRAM = BUILD_DIR / "task1" / f"160128{EXE}"
DEFAULT_VERIFIER = BUILD_DIR / "task1" / f"verifier{EXE}"
DEFAULT_INSTANCES = os.environ.get("PTSZ_INSTANCES", str(TASK_DIR / "instances"))
DEFAULT_RESULTS = os.environ.get("PTSZ_RESULTS", str(TASK_DIR / "results"))

INSTANCE_RE = re.compile(r"^in_(\d+)_(\d+)\.txt$")
OUT_FULL_RE = re.compile(r"^out_(\d+)_(\d+)_(\d+)\.txt$")  # out_<instancja>_<program>_<n>
OUT_SHORT_RE = re.compile(r"^out_(\d+)_(\d+)\.txt$")        # out_<instancja>_<n>
ERROR_TEXT = "BŁĄD"


def cmd_build(args):
    steps = [
        ["cmake", "-S", str(ROOT), "-B", str(BUILD_DIR), f"-DCMAKE_BUILD_TYPE={args.config}"],
        ["cmake", "--build", str(BUILD_DIR), "--config", args.config],
    ]
    for step in steps:
        print("$", " ".join(step), flush=True)
        if subprocess.run(step).returncode != 0:
            print("Budowanie nie powiodlo sie", file=sys.stderr)
            return 1
    return 0


def find_instances(args):
    files = list(getattr(args, "files", None) or []) + list(args.inst_files or [])
    if files:
        paths = [Path(f) for f in files]
    else:
        paths = sorted(Path(args.instances).glob("in_*_*.txt"))

    result = []
    for path in paths:
        if not path.is_file():
            print(f"Brak pliku: {path}", file=sys.stderr)
            continue
        m = INSTANCE_RE.match(path.name)
        if m:
            author, n = m.group(1), int(m.group(2))
        else:  # inna nazwa - n bierzemy z pliku
            author, n = path.stem, int(path.read_text().split()[0])
        if args.id and author not in args.id:
            continue
        if args.n and n not in args.n:
            continue
        result.append((path, author, n))
    return sorted(result, key=lambda x: (x[1], x[2]))


def find_outputs(args):
    """(plik_out, instancja albo None, autor_instancji, program, n), posortowane jak w arkuszu."""
    if args.files:
        paths = [Path(f) for f in args.files]
    else:
        paths = sorted(Path(args.results).glob("out_*.txt"))
    inst_dir = Path(args.instances)

    result = []
    for path in paths:
        if not path.is_file():
            print(f"Brak pliku: {path}", file=sys.stderr)
            continue
        if m := OUT_FULL_RE.match(path.name):
            author, prog, n = m.group(1), m.group(2), int(m.group(3))
            inst = inst_dir / f"in_{author}_{n}.txt"
        elif m := OUT_SHORT_RE.match(path.name):
            author, prog, n = m.group(1), "", int(m.group(2))
            inst = inst_dir / f"in_{author}_{n}.txt"
        else:  # np. out_50A.txt - bierzemy ostatnia liczbe jako n i jedyna pasujaca instancje
            nums = re.findall(r"\d+", path.stem)
            if not nums:
                print(f"Pomijam {path.name}: brak n w nazwie", file=sys.stderr)
                continue
            author, prog, n = "", "", int(nums[-1])
            found = list(inst_dir.glob(f"*_{n}.txt"))
            inst = found[0] if len(found) == 1 else None
        if args.id and author not in args.id:
            continue
        if args.prog_id and prog not in args.prog_id:
            continue
        if args.n and n not in args.n:
            continue
        if inst is not None and not inst.is_file():
            inst = None
        result.append((path, inst, author, prog, n))
    return sorted(result, key=lambda x: (x[2], x[3], x[4], x[0].name))


def verify(verifier, instance, solution):
    """(status, obliczona, zadeklarowana); status "OK" albo opis bledu."""
    proc = subprocess.run([str(verifier), str(instance), str(solution)],
                          capture_output=True, text=True)
    m = re.search(r"Obliczona wartosc kryterium: (\d+)", proc.stdout)
    computed = m.group(1) if m else "-"
    m = re.search(r"Zadeklarowana wartosc:\s+(-?\d+)", proc.stdout)
    declared = m.group(1) if m else "-"
    if proc.returncode == 0:
        return "OK", computed, declared
    errors = [l for l in (proc.stdout + proc.stderr).splitlines() if l.startswith("BLAD")]
    return ("; ".join(errors[:2]) if errors else "BLAD"), computed, declared


def fmt_time(seconds, args):
    s = f"{seconds:.3f}"
    return s.replace(".", ",") if args.comma else s


def check_tools(paths):
    for label, path in paths:
        if not Path(path).exists():
            print(f"Nie znaleziono: {label} {path}\n"
                  f"Zbuduj: python3 {Path(__file__).name} build", file=sys.stderr)
            return False
    return True


def write_csv(path, rows):
    if not path or not rows:
        return
    with open(path, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)
    print(f"Zapisano: {path}", file=sys.stderr)


def cmd_run(args):
    do_verify = not args.no_verify
    program_cmd = getattr(args, "program_cmd", None) or [str(args.program)]
    tools = [("weryfikator", args.verifier)] if do_verify else []
    if not getattr(args, "program_cmd", None):
        tools.append(("program", args.program))
    if not check_tools(tools):
        return 1

    instances = find_instances(args)
    if not instances:
        print("Nie znaleziono pasujacych instancji", file=sys.stderr)
        return 1

    # rozgrzewka: pierwsze uruchomienie nowego pliku (np. skanowanie przez macOS)
    # potrafi trwac dluzej i psuloby pomiar pierwszej instancji
    if not args.no_warmup:
        try:
            subprocess.run(program_cmd, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL, timeout=10)
        except (subprocess.TimeoutExpired, OSError):
            pass

    results_dir = Path(args.results)
    results_dir.mkdir(parents=True, exist_ok=True)

    table = args.only is None
    header = f"{'INSTANCJA':<20} {'LIMIT':>7} {'CZAS [s]':>9} {'WYNIK':>8}  STATUS"
    if table:
        print(header)
        print("-" * len(header))

    rows = []
    failures = 0
    for path, author, n in instances:
        limit = args.limit if args.limit is not None else n / 10
        prog_part = f"_{args.prog_id}" if args.prog_id else ""
        out = results_dir / f"out_{author}{prog_part}_{n}.txt"
        if out.exists():
            out.unlink()  # zeby nie sprawdzic starego wyniku

        cmd = program_cmd + [str(path), str(out), f"{limit:g}"]
        killed = False
        t0 = time.perf_counter()
        try:
            proc = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                                  text=True, timeout=limit + args.kill_after)
            code = proc.returncode
            stderr = proc.stderr.strip()
        except subprocess.TimeoutExpired:
            killed = True
            code, stderr = None, ""
        elapsed = time.perf_counter() - t0
        over = elapsed > limit

        if killed:
            status, value = f"PRZERWANY po {elapsed:.1f} s", "-"
        elif code != 0:
            status, value = f"BLAD: kod wyjscia {code}" + (f" ({stderr.splitlines()[-1]})" if stderr else ""), "-"
        elif not out.exists():
            status, value = "BLAD: brak pliku wynikowego", "-"
        elif do_verify:
            status, value, _ = verify(args.verifier, path, out)
        else:
            first = out.read_text().split()
            status, value = "niezweryfikowany", first[0] if first else "-"
        ok = status in ("OK", "niezweryfikowany")
        if over and not killed:
            status += " (przekroczony limit)"
        if not ok or over:
            failures += 1

        time_cell = fmt_time(elapsed, args) if not over else f"CZAS ={fmt_time(elapsed, args)}"
        if args.only == "wynik":
            print(value if ok else ERROR_TEXT, flush=True)
        elif args.only == "czas":
            print(time_cell, flush=True)
        else:
            print(f"{path.name:<20} {limit:>7g} {time_cell:>9} {value:>8}  {status}", flush=True)
        if not table and status != "OK":
            print(f"{path.name}: {status}", file=sys.stderr)

        rows.append({"instancja": path.name, "autor": author, "n": n, "limit_s": f"{limit:g}",
                     "czas_s": f"{elapsed:.3f}", "przekroczony": int(over),
                     "wynik": value, "status": status, "plik_wynikowy": str(out)})

    total_time = sum(float(r["czas_s"]) for r in rows)
    summary = f"Instancji: {len(rows)}, bledy: {failures}, laczny czas: {total_time:.2f} s"
    if table:
        print("-" * len(header))
        print(summary)
    else:
        print(summary, file=sys.stderr)
    write_csv(args.csv, rows)
    return 1 if failures else 0


def cmd_external(args):
    """Cudzy program: .cpp kompilujemy, .py/.jar przez interpreter, reszte odpalamy wprost."""
    src = Path(args.source).resolve()
    if not src.exists():
        print(f"Brak pliku: {src}", file=sys.stderr)
        return 1
    if not args.prog_id:  # 155347.cpp -> 155347
        nums = re.findall(r"\d+", src.stem)
        args.prog_id = nums[-1] if nums else src.stem

    ext = src.suffix.lower()
    if ext in (".cpp", ".cc", ".cxx"):
        exe = BUILD_DIR / "external" / f"{src.stem}{EXE}"
        exe.parent.mkdir(parents=True, exist_ok=True)
        cmd = [args.cxx, "-O2", "-std=c++17", "-o", str(exe), str(src)] + shlex.split(args.cxx_flags)
        print("$", " ".join(cmd), file=sys.stderr, flush=True)
        if subprocess.run(cmd).returncode != 0:
            print("Kompilacja nie powiodla sie", file=sys.stderr)
            return 1
        args.program_cmd = [str(exe)]
    elif ext == ".py":
        args.program_cmd = [sys.executable, str(src)]
    elif ext == ".jar":
        args.program_cmd = ["java", "-jar", str(src)]
    else:
        args.program_cmd = [str(src)]
    return cmd_run(args)


def cmd_verify(args):
    if not check_tools([("weryfikator", args.verifier)]):
        return 1
    outputs = find_outputs(args)
    if not outputs:
        print("Nie znaleziono pasujacych plikow out_*.txt", file=sys.stderr)
        return 1

    table = args.only is None
    header = f"{'ROZWIAZANIE':<28} {'INSTANCJA':<20} {'W PLIKU':>9} {'OBLICZONA':>10}  STATUS"
    if table:
        print(header)
        print("-" * len(header))

    rows = []
    failures = 0
    for out, inst, author, prog, n in outputs:
        if inst is None:
            status, computed, declared = "BLAD: nie znaleziono instancji", "-", "-"
        else:
            status, computed, declared = verify(args.verifier, inst, out)
        if status != "OK":
            failures += 1

        if args.only == "wynik":
            # --computed: wartosc policzona nawet gdy nie zgadza sie z plikiem (rozwiazania startowe)
            usable = status == "OK" or (args.computed and computed != "-")
            print(computed if usable else ERROR_TEXT, flush=True)
            if status != "OK":
                print(f"{out.name}: {status}", file=sys.stderr)
        else:
            inst_name = inst.name if inst else "-"
            print(f"{out.name:<28} {inst_name:<20} {declared:>9} {computed:>10}  {status}", flush=True)

        rows.append({"rozwiazanie": out.name, "instancja": inst.name if inst else "", "autor": author,
                     "program": prog, "n": n, "w_pliku": declared, "obliczona": computed,
                     "status": status})

    summary = f"Plikow: {len(rows)}, poprawne: {len(rows) - failures}, bledne: {failures}"
    if table:
        print("-" * len(header))
        print(summary)
    else:
        print(summary, file=sys.stderr)
    write_csv(args.csv, rows)
    return 1 if failures else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)

    b = sub.add_parser("build", help="zbuduj generator, weryfikator i algorytm (CMake)")
    b.add_argument("--config", default="Release", help="typ builda (domyslnie Release)")
    b.set_defaults(func=cmd_build)

    def add_run_args(p):
        p.add_argument("-i", "--instances", default=DEFAULT_INSTANCES,
                       help="katalog z instancjami (domyslnie $PTSZ_INSTANCES albo task1/instances)")
        p.add_argument("-r", "--results", default=DEFAULT_RESULTS,
                       help="katalog na pliki wynikowe (domyslnie $PTSZ_RESULTS albo task1/results)")
        p.add_argument("--inst", nargs="+", dest="inst_files", default=[],
                       help="pojedyncze pliki instancji zamiast calego katalogu")
        p.add_argument("--id", nargs="+", help="tylko instancje tych autorow (indeksy)")
        p.add_argument("--n", nargs="+", type=int, help="tylko instancje o tych rozmiarach")
        p.add_argument("--limit", type=float, help="limit czasu w s (domyslnie n/10)")
        p.add_argument("--kill-after", type=float, default=5.0,
                       help="przerwij program po przekroczeniu limitu o tyle sekund (domyslnie 5)")
        p.add_argument("--prog-id", help="indeks autora programu - nazwy out_<instancja>_<prog-id>_<n>.txt")
        p.add_argument("--verifier", default=str(DEFAULT_VERIFIER), help="plik wykonywalny weryfikatora")
        p.add_argument("--no-verify", action="store_true", help="nie uruchamiaj weryfikatora")
        p.add_argument("--no-warmup", action="store_true", help="bez rozgrzewkowego uruchomienia programu")
        p.add_argument("--only", choices=["wynik", "czas"], help="wypisz tylko jedna kolumne do wklejenia")
        p.add_argument("--comma", action="store_true", help="przecinek dziesietny w czasach (arkusz po polsku)")
        p.add_argument("--csv", help="zapisz wyniki do pliku CSV")

    r = sub.add_parser("run", help="uruchom algorytm dla instancji z pomiarem czasu")
    r.add_argument("files", nargs="*", help="pojedyncze pliki instancji (domyslnie: caly katalog)")
    add_run_args(r)
    r.add_argument("--program", default=str(DEFAULT_PROGRAM), help="plik wykonywalny algorytmu")
    r.set_defaults(func=cmd_run)

    e = sub.add_parser("external", help="uruchom cudzy program (.cpp, .py, .jar albo plik wykonywalny)")
    e.add_argument("source", help="np. 155347.cpp, 155347.py, ./155347")
    add_run_args(e)
    e.add_argument("--cxx", default=os.environ.get("CXX", "c++"), help="kompilator dla .cpp (domyslnie c++)")
    e.add_argument("--cxx-flags", default="", help='dodatkowe flagi kompilacji, np. --cxx-flags="-O3 -march=native"')
    e.set_defaults(func=cmd_external)

    v = sub.add_parser("verify", help="sprawdz weryfikatorem gotowe pliki out_*.txt")
    v.add_argument("files", nargs="*", help="pojedyncze pliki out (domyslnie: caly katalog --results)")
    v.add_argument("-r", "--results", default=DEFAULT_RESULTS,
                   help="katalog z plikami out_*.txt (domyslnie $PTSZ_RESULTS albo task1/results)")
    v.add_argument("-i", "--instances", default=DEFAULT_INSTANCES,
                   help="katalog z instancjami (domyslnie $PTSZ_INSTANCES albo task1/instances)")
    v.add_argument("--id", nargs="+", help="tylko instancje tych autorow")
    v.add_argument("--prog-id", nargs="+", help="tylko wyniki programow tych autorow")
    v.add_argument("--n", nargs="+", type=int, help="tylko te rozmiary")
    v.add_argument("--verifier", default=str(DEFAULT_VERIFIER), help="plik wykonywalny weryfikatora")
    v.add_argument("--only", choices=["wynik"], help="wypisz tylko kolumne wynikow do wklejenia")
    v.add_argument("--computed", action="store_true",
                   help="z --only wynik: wypisz policzona wartosc nawet gdy w pliku jest inna")
    v.add_argument("--csv", help="zapisz wyniki do pliku CSV")
    v.set_defaults(func=cmd_verify)

    args = parser.parse_args()
    sys.exit(args.func(args))


if __name__ == "__main__":
    main()
