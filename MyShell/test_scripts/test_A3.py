#!/usr/bin/python3

import argparse
import os
import sys
import subprocess
import glob
import re
from collections import Counter

RED = "\033[0;31m"
GREEN = "\033[0;32m"
CYAN = "\033[1;36m"
NC = "\033[0m"

MACRO_PATTERN = re.compile(r"Frame Store Size = (\d+); Variable Store Size = (\d+)")


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


def rebuild(build_dir: str, prev_dir: str, frame_size: str, var_size: str):
    os.chdir(build_dir)

    res = subprocess.run(["make", "clean", "--silent"])
    if res.returncode != 0:
        print(f"{RED}Failed to run `make clean`")
        return False

    subprocess.run(
        [
            "make",
            "mysh",
            f"framesize={frame_size}",
            f"varmemsize={var_size}",
            "--silent",
        ]
    )
    if res.returncode != 0:
        print(f"{RED}Failed to build executable.")
        return False

    os.chdir(prev_dir)


def run_test(
    prog: str,
    test_dir: str,
    test_file: str,
    non_deterministic: set[str],
    read_macros: bool,
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
        with open(result) as f:
            result_lines = f.read().splitlines()

        if read_macros:
            matches = re.findall(MACRO_PATTERN, result_lines[0])
            if not matches:
                print(f"{RED}Failed to read macro values from result file.")
                return False
            frame_size, var_size = matches[0]
            rebuild(os.path.dirname(executable), test_dir, frame_size, var_size)

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
            sim, a_extra, b_extra = count_similarity(lines, Counter(result_lines))
            if sim == 1:
                passed = True
                break

            diff = "(MULTITHREAD DIFF)"
            newline = "\n"
            if a_extra:
                diff += f"\nExtra lines from actual output:\n{newline.join(a_extra)}"
            if b_extra:
                diff += f"\nExtra lines from expected output:\n{newline.join(b_extra)}"
    if passed:
        print(f"{GREEN} Passed")
    else:
        print(f"{RED} Failed\n---Diff---{NC}")
        print(diff)
        print(f"{RED}---End diff---{NC}")

    os.chdir(cur_dir)

    return passed


def run_multiple(
    prog: str, test_dir: str, non_deterministic: set[str], read_macros: bool
):
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
        if run_test(prog, test_dir, test, non_deterministic, read_macros):
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
    parser.add_argument(
        "-r",
        "--read-macros",
        help="Whether or not to read compile macro values from the result file.",
        action=argparse.BooleanOptionalAction,
    )

    args = parser.parse_args()
    prog = args.prog[0]

    if not args.read_macros and (
        not os.path.isfile(prog) or not os.access(prog, os.X_OK)
    ):
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
        run_test(
            prog,
            os.path.realpath(os.path.dirname(args.test)),
            args.test,
            non_deterministic,
            args.read_macros,
        )
    else:
        run_multiple(
            prog, os.path.realpath(args.test), non_deterministic, args.read_macros
        )


if __name__ == "__main__":
    main()


