## Authors
Marc-Antoine Nadeau <br/>
Karl Bridi <br/>

A lightweight simulation of a basic operating system shell written in C. It supports command execution, process scheduling, and memory management through demand paging and LRU replacement.

---

## Features

- Interactive and batch shell modes
- Built-in commands: `help`, `quit`, `set`, `print`, `source`, `echo`
- File and directory utilities: `my_ls`, `my_mkdir`, `my_touch`, `my_cd`
- One-liner command chaining with `;`
- `run` command for launching external programs using `fork-exec-wait`
- `exec` command to run up to 3 concurrent scripts with scheduling
- Supported scheduling policies:
  - `FCFS` – First Come First Serve
  - `SJF` – Shortest Job First
  - `RR` – Round Robin (time slice: 2)
  - `AGING` – SJF with aging to prevent starvation
  - `RR30` – Extended time slice round-robin (30 instructions)
- Background execution with `exec ... POLICY #`
- Demand paging with 3-line page size
- LRU (Least Recently Used) page replacement policy
- Shared pages between processes executing the same program
- Compile-time configuration of memory limits

---

## Compilation

You can configure memory size at compile time:

```
make mysh framesize=12 varmemsize=20
```

On startup, the shell will display:

```
Frame Store Size = 12; Variable Store Size = 20
```

---

## Usage

### Run Shell

- **Interactive mode**:
  ```
  ./mysh
  ```

- **Batch mode**:
  ```
  ./mysh < script.txt
  ```

---

## Example Commands

```
set x 42
print x
echo $x

my_mkdir demo
my_touch file1
my_ls
my_cd demo

run cat file1.txt
exec prog1 prog2 prog3 RR
exec prog1 RR #
```

---

## Paging & Memory Management

- Only first two pages of each program (3 lines per page) are initially loaded.
- Additional pages are loaded on-demand during execution (page faults).
- When memory is full, the least recently used page is evicted.
- Page fault messages and evicted page contents are printed to the terminal.