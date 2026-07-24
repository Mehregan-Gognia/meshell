# meshell

A minimalist, test-driven POSIX-flavored Unix shell implemented in C. 

This project explores low-level systems programming concepts—focusing on the Unix process lifecycle, memory isolation, structural stream control, file descriptor manipulation, and recursive execution syntax parsing. It fully satisfies the compliance tests for process management, concurrent pipelines, and stream redirections in the `build-your-own-shell` workshop framework.

---

## Key Features

### Interactive Experience
* **GNU Readline Integration:** Provides a rich interactive prompt with line editing capabilities.
* **Smart Tab Completion:** Context-aware auto-completion that intelligently suggests built-in commands and executables from `$PATH` for the first word, falling back to standard filename completion for arguments.
* **Persistent History:** Automatically logs session commands to `$HISTFILE` (or `~/.meshell_history`) and features a built-in `history` command alongside `Ctrl+R` reverse search.

### Process & Job Control
* **Process Lifecycle Control:** Native execution using `fork(2)`, `execvp(3)`, and status collection via `waitpid(2)`[cite: 1].
* **Job Control:** Run tasks asynchronously using the background operator `&`, and seamlessly manage them with `fg` (bring to foreground) and `bg` (resume in background)[cite: 1].
* **Subshell Sandbox Isolation:** Parenthetical groups `(commands)` are captured safely and evaluated using process cloning and **recursive execution**, ensuring child state alterations (such as directory changes) never leak into the parent environment[cite: 4].
* **Pipeline Negation:** Implements the POSIX pipeline prefix operator `!` to accurately invert execution exit statuses, with precise structural parsing to handle mid-pipeline literals correctly[cite: 4].

### Execution & Logic
* **Built-in Commands:** Context-aware handling of `cd` (with environmental fallback to `$HOME`), `export` for variable promotion, `exit` with custom exit codes, and process-replacing `exec` (safely handled as a no-op when bare)[cite: 1].
* **Short-Circuit Logic Chaining:** Fully supports sequential list execution via `;` alongside left-to-right conditional short-circuiting with `&&` and `||`[cite: 4].
* **Line Continuation:** Employs multi-line input buffer accumulation inside the primary REPL loop whenever lines terminate with an unescaped backslash `\`[cite: 2].

### Expansions & Parsing
* **Variable & Status Expansion:** Dynamically resolves environment variables (`$VAR`), local shell variables, the last exit status (`$?`), and the last background PID (`$!`)[cite: 7].
* **Tilde & Glob Expansion:** Natively resolves user home directories (`~` and `~username`) and processes wildcard globbing (`*`, `?`)[cite: 1, 7].
* **Recursive Command Substitution:** Supports inline expression evaluation and expansion of nested `$(...)` blocks by capturing background pipe output streams recursively, complete with POSIX-compliant trailing newline stripping[cite: 7].

### Advanced I/O & Pipelines
* **Concurrent Pipeline Execution:** Simultaneous multi-stage pipeline processing (`cmd1 | cmd2 | cmd3`) utilizing shared kernel pipe buffers via `pipe(2)` and standard stream duplication via `dup2(2)`[cite: 1].
* **Advanced Stream Redirection:** A generic redirection engine supporting standard file overwriting, appending, and bidirectional read/write streams (`<`, `>`, `>>`, `<>`). It dynamically parses explicit file descriptor prefixes (e.g., `3<`, `1<>`) and handles descriptor closure or duplication routing tables (`<&-`, `>&-`, `2>&1`) cleanly[cite: 5].
* **Heredocs:** Processes inline multi-line text streams (`<<`) securely, supporting both literal and dynamically expanded delimiter states[cite: 5].

---

## Architectural Notes

### Modular File Structure
To maintain strong separation of concerns, the project is decoupled from a single monolithic file into clean, modular components governed by a unified shared header (`shell.h`):
* `main.c`: Coordinates the primary REPL input loop, interactive session setups, and multi-line buffer accumulation[cite: 2].
* `parser.c`: Conducts structural command boundary analysis and conditional flow evaluation[cite: 4].
* `executor.c`: Controls low-level process fork mechanics, job control, child status monitoring, and native execution[cite: 1].
* `completion.c`: Hooks into GNU Readline to provide dynamic, context-aware tab completion for binaries and built-ins.
* `substitution.c`: Manages background capture channels, globbing, and dynamic string mapping for variables and nested `$(...)` blocks[cite: 7].
* `redirection.c`: Processes the generic file descriptor table adjustments, file stream openings, Heredoc generation, and descriptor closures[cite: 5].

### Protected Single-Pass Scanner
`meshell` uses a stateful, linear left-to-right stream scanner to identify execution boundaries[cite: 4]. Unlike naive tokenizers that prematurely split on parentheses or pipe symbols, this scanner treats subshell blocks `(...)` and command substitution regions `$(...)` as protected contexts. 

It sweeps the top-level string chunk by chunk, tracking conditional states dynamically while skipping nested boundaries until an expansion pass or an execution fork isolates them. This architecture ensures stability when processing compound streams like:
```bash
echo mitten >foo && echo -n k 1<>foo && cat $(echo foo)
