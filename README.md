# meshell

A minimalist, test-driven POSIX-flavored Unix shell implemented in C. 

This project explores lower-level systems programming concepts—focusing on the Unix process lifecycle, memory isolation, structural stream control, and recursive execution syntax parsing. It fully satisfies the compliance tests for process management in the `build-your-own-shell` workshop framework.

---

## Key Features

* **Process Lifecycle Control:** Native execution using `fork(2)`, `execvp(3)`, and status collection via `waitpid(2)`.
* **Built-in Commands:** Context-aware handling of `cd` (including environmental fallback to `$HOME`), `exit` with custom exit codes, and process-replacing `exec`.
* **Short-Circuit Logic Chaining:** Fully supports sequential list execution via `;` alongside left-to-right conditional short-circuiting with `&&` and `||`.
* **Subshell Sandbox Isolation:** Parenthetical groups `(commands)` are captured safely and evaluated using process cloning and **recursive execution**, ensuring child state alterations (such as directory changes) never leak into the parent environment.
* **Pipeline Negation:** Implements the POSIX pipeline prefix operator `!` to accurately invert execution exit statuses.
* **Line Continuation:** Employs multi-line input buffer accumulation inside the primary REPL loop whenever lines terminate with an unescaped backslash `\`.

---

## Architectural Notes

### Single-Pass Linear State Scanner
While typical naive shells rely heavily on standard tokenizers like `strtok`, doing so destroys parenthetical subshell boundaries and logical chains prematurely. 

`meshell` uses a stateful, linear left-to-right stream scanner. It sweeps through the command line chunk by chunk, tracking conditional states dynamically. When a subshell marker `(` is hit, the scanner counts parenthesis depth to isolate the block, forks an execution sandbox, and evaluates the internal string recursively—preserving syntax integrity across complex lines like `(exit 0 && exit 1) || echo success`.

---

## Building and Running

### Prerequisites
To compile the shell, you need a standard C compiler (`gcc` or `clang`). To run the verification suite, the `expect` package must be present on your system.

### Compilation
Compile the source file using standard optimization and warning flags:

```shell
gcc -Wall -Wextra -O2 -o meshell meshell.c
```

---

## Acknowledgments

This implementation was built as part of the [build-your-own-shell](https://github.com/tokenrove/build-your-own-shell) workshop framework created by [tokenrove](https://github.com/tokenrove). 

Special thanks to the author for providing the rigorous `expect`-based test suites (`./validate`) and the structural roadmap that made verifying this shell's POSIX compliance possible.
