#!/usr/bin/env python3
"""Budowanie i odpalanie algorytmu dla zad. 1 z pomiarem czasu.

  python3 task1/run.py build
  python3 task1/run.py run                          # wszystko z task1/instances
  python3 task1/run.py run --id 160128 --n 50 500
  python3 task1/run.py run task1/instances/in_160128_50.txt
  python3 task1/run.py run --program ./155347 --prog-id 155347 --csv wyniki.csv

Czas to czas rzeczywisty procesu. Limit domyslnie n/10 s, po przekroczeniu
"CZAS =X", a po limit + --kill-after s proces jest ubijany.
"""
import argparse
import csv
import os
import re
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

INSTANCE_RE = re.compile(r"^in_(\d+)_(\d+)\.txt$")


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
    if args.files:
        paths = [Path(f) for f in args.files]
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


def verify(verifier, instance, solution):
    proc = subprocess.run([str(verifier), str(instance), str(solution)],
                          capture_output=True, text=True)
    m = re.search(r"Obliczona wartosc kryterium: (\d+)", proc.stdout)
    value = m.group(1) if m else "-"
    if proc.returncode == 0:
        return "OK", value
    errors = [l for l in (proc.stdout + proc.stderr).splitlines() if l.startswith("BLAD")]
    return "BLAD: " + "; ".join(errors[:2]) if errors else "BLAD", value


def cmd_run(args):
    program = Path(args.program)
    if not program.exists():
        print(f"Nie znaleziono programu: {program}\n"
              f"Zbuduj go: python3 {Path(__file__).name} build", file=sys.stderr)
        return 1
    verifier = Path(args.verifier)
    do_verify = not args.no_verify
    if do_verify and not verifier.exists():
        print(f"Nie znaleziono weryfikatora: {verifier} (uzyj --no-verify)", file=sys.stderr)
        return 1

    instances = find_instances(args)
    if not instances:
        print("Nie znaleziono pasujacych instancji", file=sys.stderr)
        return 1

    results_dir = Path(args.results)
    results_dir.mkdir(parents=True, exist_ok=True)

    header = f"{'INSTANCJA':<20} {'LIMIT':>7} {'CZAS [s]':>9} {'WYNIK':>8}  STATUS"
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

        cmd = [str(program), str(path), str(out), f"{limit:g}"]
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

        time_cell = f"{elapsed:.3f}" if elapsed <= limit else f"CZAS ={elapsed:.3f}"
        if killed:
            status, value = f"PRZERWANY po {elapsed:.1f} s", "-"
        elif code != 0:
            status, value = f"BLAD: kod wyjscia {code}" + (f" ({stderr.splitlines()[-1]})" if stderr else ""), "-"
        elif not out.exists():
            status, value = "BLAD: brak pliku wynikowego", "-"
        elif do_verify:
            status, value = verify(verifier, path, out)
        else:
            first = out.read_text().split()
            status, value = "niezweryfikowany", first[0] if first else "-"
        if elapsed > limit and not killed:
            status += " (przekroczony limit)"
        if status != "OK" and status != "niezweryfikowany":
            failures += 1

        print(f"{path.name:<20} {limit:>7g} {time_cell:>9} {value:>8}  {status}", flush=True)
        rows.append({"instancja": path.name, "autor": author, "n": n, "limit_s": f"{limit:g}",
                     "czas_s": f"{elapsed:.3f}", "przekroczony": int(elapsed > limit),
                     "wynik": value, "status": status, "plik_wynikowy": str(out)})

    total_time = sum(float(r["czas_s"]) for r in rows)
    print("-" * len(header))
    print(f"Instancji: {len(rows)}, bledy: {failures}, laczny czas: {total_time:.2f} s")

    if args.csv:
        with open(args.csv, "w", newline="") as f:
            writer = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
            writer.writeheader()
            writer.writerows(rows)
        print(f"Zapisano: {args.csv}")
    return 1 if failures else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)

    b = sub.add_parser("build", help="zbuduj generator, weryfikator i algorytm (CMake)")
    b.add_argument("--config", default="Release", help="typ builda (domyslnie Release)")
    b.set_defaults(func=cmd_build)

    r = sub.add_parser("run", help="uruchom algorytm dla instancji z pomiarem czasu")
    r.add_argument("files", nargs="*", help="pojedyncze pliki instancji (domyslnie: caly katalog)")
    r.add_argument("--instances", default=str(TASK_DIR / "instances"), help="katalog z instancjami")
    r.add_argument("--results", default=str(TASK_DIR / "results"), help="katalog na pliki wynikowe")
    r.add_argument("--id", nargs="+", help="tylko instancje tych autorow (indeksy)")
    r.add_argument("--n", nargs="+", type=int, help="tylko instancje o tych rozmiarach")
    r.add_argument("--limit", type=float, help="limit czasu w s (domyslnie n/10)")
    r.add_argument("--kill-after", type=float, default=5.0,
                   help="przerwij program po przekroczeniu limitu o tyle sekund (domyslnie 5)")
    r.add_argument("--program", default=str(DEFAULT_PROGRAM), help="plik wykonywalny algorytmu")
    r.add_argument("--prog-id", help="indeks autora programu - nazwy out_<instancja>_<prog-id>_<n>.txt")
    r.add_argument("--verifier", default=str(DEFAULT_VERIFIER), help="plik wykonywalny weryfikatora")
    r.add_argument("--no-verify", action="store_true", help="nie uruchamiaj weryfikatora")
    r.add_argument("--csv", help="zapisz wyniki do pliku CSV")
    r.set_defaults(func=cmd_run)

    args = parser.parse_args()
    sys.exit(args.func(args))


if __name__ == "__main__":
    main()
