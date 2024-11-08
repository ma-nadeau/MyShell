#!/usr/bin/python3

import argparse
import os
import sys
import subprocess
import glob
from collections import Counter

RED = "\033[0;31m"
GREEN = "\033[0;32m"
CYAN = "\033[1;36m"
NC = "\033[0m"
newline = '\n'


def count_similarity(a: Counter, b: Counter):
    a_extra = []
    b_extra = []
    total = sum(max(a.get(k, 0), b.get(k, 0)) for k in a | b)
    inter = 0

    for k in a | b:
        a_count = a.get(k, 0)
        b_count = b.get(k, 0)
        if a_count < b_count:
            b_extra.extend([k] * (b_count - a_count))
        elif a_count > b_count:
            a_extra.extend([k] * (a_count - b_count))
        inter += min(a_count, b_count)

    return inter / total, a_extra, b_extra


def run_test(
    prog: str, test_dir: str, test_file: str, non_deterministic: set[str]
) -> bool:
    executable = os.path.realpath(prog)
    cur_dir = os.getcwd()
    test_name = test_file.rstrip(".txt")

    print(f"{NC}Running {test_name}...", end="")
    os.chdir(test_dir)

    passed = False
    diff = None
    with open(test_file, "r") as f:
        input_lines = f.read()

    for result in glob.glob(f"{test_name}_result*.txt"):
        shell_res = subprocess.run(
            [executable],
            input=input_lines.encode(),
            capture_output=True,
        )
        if test_name not in non_deterministic:
            res = subprocess.run(
                ["diff", "-w", "-y", "--color=always", result, "-"],
                input=shell_res.stdout,
                capture_output=True,
            )
            diff = res.stdout.decode()
            if res.returncode == 0:
                passed = True
                break
        else:
            lines = Counter(shell_res.stdout.decode().splitlines())
            with open(result) as f:
                result_lines = Counter(f.read().splitlines())
            sim, a_extra, b_extra = count_similarity(lines, result_lines)
            if sim == 1:
                passed = True
                break
            diff = "(MULTITHREAD DIFF)"
            if a_extra:
                diff += f"{newline}Extra lines from actual output:{newline}{newline.join(a_extra)}"
            if b_extra:
                diff += f"{newline}Extra lines from expected output:{newline}{newline.join(b_extra)}"
    if passed:
        print(f"{GREEN} Passed")
    else:
        print(f"{RED} Failed{newline}---Diff---{NC}")
        print(diff)
        print(f"{RED}---End diff---{NC}")

    os.chdir(cur_dir)

    return passed


def run_multiple(prog: str, test_dir: str, non_deterministic: set[str]):
    def is_test(f):
        return (
            os.path.isfile(os.path.join(test_dir, f))
            and f.endswith(".txt")
            and "_result" not in f
        )

    tests = sorted(filter(is_test, os.listdir(test_dir)))

    passed = 0
    failed = 0

    for test in tests:
        if run_test(prog, test_dir, test, non_deterministic):
            passed += 1
        else:
            failed += 1

    print(f"{NC}---Summary---")
    print(f"{GREEN}Passed: {passed}")
    print(f"{RED}Failed: {failed}")
    print(f"{NC}Total: {len(tests)}")


def main():
    parser = argparse.ArgumentParser(
        prog="test.py", description="Testcase runner for COMP310"
    )
    parser.add_argument("prog", nargs=1)
    parser.add_argument(
        "-t", "--test", help="Test file or directory of tests to run.", required=True
    )
    parser.add_argument(
        "-nd",
        help="A file containing a list of names of non-deterministic tests.",
    )

    args = parser.parse_args()
    prog = args.prog[0]

    if not os.path.isfile(prog) or not os.access(prog, os.X_OK):
        print(
            f"Error: file '{prog}' does not exist or is not executable",
            file=sys.stderr,
        )
        sys.exit(1)

    if not os.path.exists(args.test):
        print(
            f"Error: '{args.test}' does not exist",
            file=sys.stderr,
        )
        sys.exit(1)

    if not os.path.exists(args.test):
        print(
            f"Error: '{args.test}' does not exist",
            file=sys.stderr,
        )
        sys.exit(1)

    non_deterministic = set()
    if args.nd is not None:
        if not os.path.isfile(args.nd):
            print(
                f"Error: file '{args.nd}' does not exist",
                file=sys.stderr,
            )
            sys.exit(1)

        with open(args.nd, "r") as f:
            non_deterministic.update(line.strip() for line in f.readlines())

    if os.path.isfile(args.test):
        run_test(prog, os.path.dirname(args.test), args.test, non_deterministic)
    else:
        run_multiple(prog, args.test, non_deterministic)

    return


if __name__ == "__main__":
    main()
