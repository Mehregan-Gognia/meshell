# meshell

A minimalist, test-driven POSIX-flavored Unix shell implemented in C. 

This project explores low-level systems programming concepts—focusing on the Unix process lifecycle, memory isolation, structural stream control, file descriptor manipulation, and recursive execution syntax parsing. It fully satisfies the compliance tests for process management, concurrent pipelines, and stream redirections in the `build-your-own-shell` workshop framework.

---

## Key Features

* **Process Lifecycle Control:** Native execution using `fork(2)`, `execvp(3)`, and status collection via `waitpid(2)`.
* **Built-in Commands:** Context-aware handling of `cd` (including environmental fallback to `$HOME`), `exit` with custom exit codes, and process-replacing `exec` (safely handled as a no-op when bare).
* **Short-Circuit Logic Chaining:** Fully supports sequential list execution via `;` alongside left-to-right conditional short-circuiting with `&&` and `||`.
* **Subshell Sandbox Isolation:** Parenthetical groups `(commands)` are captured safely and evaluated using process cloning and **recursive execution**, ensuring child state alterations (such as directory changes) never leak into the parent environment.
* **Pipeline Negation:** Implements the POSIX pipeline prefix operator `!` to accurately invert execution exit statuses, with precise structural parsing to handle mid-pipeline literals correctly.
* **Line Continuation:** Employs multi-line input buffer accumulation inside the primary REPL loop whenever lines terminate with an unescaped backslash `\`.
* **Concurrent Pipeline Execution:** Simultaneous multi-stage pipeline processing (`cmd1 | cmd2 | cmd3`) utilizing shared kernel pipe buffers via `pipe(2)` and standard stream duplication via `dup2(2)`.
* **Advanced I/O Stream Redirection:** A generic redirection engine supporting standard file overwriting, appending, and bidirectional read/write streams (`<`, `>`, `>>`, `<>`). It dynamically parses explicit file descriptor prefixes (e.g., `3<`, `1<>`) and handles descriptor closure or duplication routing tables (`<&-`, `>&-`, `2>&1`) cleanly.
* **Recursive Command Substitution:** Supports inline expression evaluation and expansion of nested `$(...)` blocks by capturing background pipe output streams recursively, complete with POSIX-compliant trailing newline stripping.

---

## Architectural Notes

### Modular File Structure
To maintain strong separation of concerns, the project is decoupled from a single monolithic file into clean, modular components governed by a unified shared header (`shell.h`):
* `main.c`: Coordinates the primary REPL input loop and multi-line backslash buffer accumulation.
* `parser.c`: Conducts structural command boundary analysis and conditional flow evaluation.
* `executor.c`: Controls low-level process fork mechanics, child status monitoring, and native execution.
* `substitution.c`: Manages background capture channels and dynamic string mapping for nested `$(...)` blocks.
* `redirection.c`: Processes the generic file descriptor table adjustments, file stream openings, and closures.

### Protected Single-Pass Scanner
`meshell` uses a stateful, linear left-to-right stream scanner to identify execution boundaries. Unlike naive tokenizers that prematurely split on parentheses or pipe symbols, this scanner treats subshell blocks `(...)` and command substitution regions `$(...)` as protected contexts. 

It sweeps the top-level string chunk by chunk, tracking conditional states dynamically while skipping nested boundaries until an expansion pass or an execution fork isolates them. This architecture ensures stability when processing compound streams like:
```bash
echo mitten >foo && echo -n k 1<>foo && cat $(echo foo)
