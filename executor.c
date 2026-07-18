#include "shell.h"

int next_job_bg = 0;
int last_exit_status = 0;

local_var local_vars[MAX_LOCAL_VARS];
int local_var_count = 0;

void set_shell_var(const char *name, const char *value)
{
    for (int i = 0; i < local_var_count; i++)
    {
        if (strcmp(local_vars[i].name, name) == 0)
        {
            free(local_vars[i].value);
            local_vars[i].value = strdup(value);

            if (local_vars[i].is_exported)
            {
                setenv(name, value, 1);
            }

            return;
        }
    }

    if (local_var_count < MAX_LOCAL_VARS)
    {
        local_vars[local_var_count].name = strdup(name);
        local_vars[local_var_count].value = strdup(value);
        local_vars[local_var_count].is_exported = 0;
        local_var_count++;
    }
}

char *get_shell_var(const char *name)
{
    for (int i = 0; i < local_var_count; i++)
    {
        if (strcmp(local_vars[i].name, name) == 0)
        {
            return local_vars[i].value;
        }
    }
    return NULL;
}

// executes a single command with arguments, handling built in commands and forking for external commands
int execute_single_command(char **args)
{
    //* EMPTY
    if (args[0] == NULL)
    {
        return 0;
    }

    //* FG
    if (strcmp(args[0], "fg") == 0)
    {
        if (last_job_pid <= 0)
        {
            fprintf(stderr, "fg: no current job\n");
            return 1;
        }
        if (isatty(STDIN_FILENO))
        {
            tcsetpgrp(STDIN_FILENO, last_job_pid);
        }

        kill(-last_job_pid, SIGCONT); // Resume process group

        int status;
        waitpid(last_job_pid, &status, WUNTRACED);

        if (isatty(STDIN_FILENO))
        {
            tcsetpgrp(STDIN_FILENO, getpgrp()); // Reclaim terminal
        }

        if (WIFSTOPPED(status))
            return 128 + WSTOPSIG(status);
        if (WIFSIGNALED(status))
            return 128 + WTERMSIG(status);
        if (WIFEXITED(status))
            return WEXITSTATUS(status);
        return 0;
    }

    //* BG
    if (strcmp(args[0], "bg") == 0)
    {
        if (last_job_pid <= 0)
        {
            fprintf(stderr, "bg: no current job\n");
            return 1;
        }
        kill(-last_job_pid, SIGCONT); // Resume process group in background
        return 0;
    }

    //* ENV ASSIGNMENTS
    int assign_count = 0;
    while (args[assign_count] != NULL && strchr(args[assign_count], '=') != NULL)
    {
        assign_count++;
    }

    // Handle standalone assignment blocks (modify environment permanently)
    if (assign_count > 0 && args[assign_count] == NULL)
    {
        for (int j = 0; j < assign_count; j++)
        {
            char *eq = strchr(args[j], '=');
            *eq = '\0';
            set_shell_var(args[j], eq + 1);
        }

        return 0;
    }

    //* EXEC
    if (strcmp(args[0], "exec") == 0)
    {
        if (args[1] == NULL)
        {
            return 0;
        }

        char **exec_args = &args[1];

        if (execvp(exec_args[0], exec_args) == -1)
        {
            perror("exec failed");
            return 1;
        }
    }

    //* CD
    if (strcmp(args[0], "cd") == 0)
    {
        char *target_dir = args[1];
        if (target_dir == NULL)
        {
            target_dir = getenv("HOME");
        }

        if (chdir(target_dir) != 0)
        {
            perror("cd failed");
            return 1;
        }

        return 0;
    }

    //* EXIT
    if (strcmp(args[0], "exit") == 0)
    {
        int exit_code = 0;
        if (args[1] != NULL)
        {
            exit_code = atoi(args[1]);
        }

        exit(exit_code);
    }

    //* EXPORT
    if (strcmp(args[0], "export") == 0)
    {
        if (args[1] == NULL)
        {
            return 0; // Bare export is a safe no-op for compliance runners
        }

        char *var_name = args[1];
        char *eq = strchr(var_name, '=');

        // Handle inline definitions: export VAR=value
        if (eq)
        {
            *eq = '\0';
            char *val = eq + 1;
            set_shell_var(var_name, val);

            for (int i = 0; i < local_var_count; i++)
            {
                if (strcmp(local_vars[i].name, var_name) == 0)
                {
                    local_vars[i].is_exported = 1;
                    break;
                }
            }
            setenv(var_name, val, 1);
        }
        else
        {
            // Handle standard tracking: export VAR
            int found = 0;
            for (int i = 0; i < local_var_count; i++)
            {
                if (strcmp(local_vars[i].name, var_name) == 0)
                {
                    local_vars[i].is_exported = 1;
                    if (local_vars[i].value)
                    {
                        setenv(var_name, local_vars[i].value, 1); // Promote existing value
                    }
                    found = 1;
                    break;
                }
            }

            // If it doesn't exist yet, initialize an empty placeholder marked for export
            if (!found && local_var_count < MAX_LOCAL_VARS)
            {
                local_vars[local_var_count].name = strdup(var_name);
                local_vars[local_var_count].value = strdup("");
                local_vars[local_var_count].is_exported = 1;
                local_var_count++;
                setenv(var_name, "", 1);
            }
        }

        return 0;
    }

    //* FORK
    pid_t pid = fork();
    if (pid < 0)
    {
        perror("fork failed");
        return 1;
    }
    else if (pid == 0)
    {
        // child
        if (isatty(STDIN_FILENO))
        {
            setpgid(0, 0);
            if (next_job_bg == 0) //! Only claim terminal foreground if NOT a background job
            {
                tcsetpgrp(STDIN_FILENO, getpid());
            }
        }

        // Safe to reset signal actions back to defaults now
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        signal(SIGTTIN, SIG_DFL);
        signal(SIGTTOU, SIG_DFL);

        // handle environment variable assignments for this command execution
        char **actual_args = args;
        if (assign_count > 0)
        {
            for (int j = 0; j < assign_count; j++)
            {
                char *eq = strchr(args[j], '=');
                if (eq)
                {
                    *eq = '\0';
                    setenv(args[j], eq + 1, 1);
                }
            }
            actual_args = &args[assign_count];
        }

        // If no command is provided after assignments, exit successfully
        if (actual_args[0] == NULL)
        {
            exit(0);
        }

        // Handle file redirections inside the child using our shifted argument vector
        if (check_and_handle_redirections(actual_args) < 0)
        {
            exit(1);
        }

        // execute the command
        if (execvp(actual_args[0], actual_args) == -1)
        {
            perror("execvp failed");
            exit(127);
        }
    }
    else
    {
        // parent
        if (isatty(STDIN_FILENO))
        {
            setpgid(pid, pid);
            if (!next_job_bg) // FIXED: Only claim terminal foreground if NOT a background job!
            {
                tcsetpgrp(STDIN_FILENO, pid);
            }
        }

        // Return instantly to the prompt for background jobs
        if (next_job_bg)
        {
            last_job_pid = pid; // Track background pid for fg command mapping
            return 0;
        }

        int status;
        waitpid(pid, &status, WUNTRACED);

        if (isatty(STDIN_FILENO))
        {
            tcsetpgrp(STDIN_FILENO, getpgrp()); // Instantly reclaim terminal
        }

        if (WIFSTOPPED(status))
        {
            last_job_pid = pid; // Track job mapping for fg/bg routines
            return 128 + WSTOPSIG(status);
        }

        if (WIFSIGNALED(status))
        {
            return 128 + WTERMSIG(status);
        }

        if (WIFEXITED(status))
        {
            return WEXITSTATUS(status);
        }
    }

    return 1;
}

// executes a pipeline of commands separated by the pipe operator '|'
int execute_pipeline(char *line)
{
    char *cmds[MAX_CMDS];
    int num_cmds = 0;
    char *pipe_save_ptr;

    char *token = strtok_r(line, "|", &pipe_save_ptr); // break down to separate commands
    while (token != NULL)
    {
        if (num_cmds >= MAX_CMDS - 1) // prevent stack overflow
        {
            break;
        }

        cmds[num_cmds++] = token;
        token = strtok_r(NULL, "|", &pipe_save_ptr);
    }

    if (num_cmds == 0)
    {
        return 0;
    }

    // allocate pipes
    int num_pipes = num_cmds - 1;
    int *pipe_fds = malloc(sizeof(int) * 2 * num_pipes);
    for (int i = 0; i < num_pipes; i++)
    {
        if (pipe(pipe_fds + i * 2) < 0)
        {
            perror("pipe failed");
            free(pipe_fds);
            return 1;
        }
    }

    pid_t *pids = malloc(sizeof(pid_t) * num_cmds);
    pid_t pgid = 0;

    for (int i = 0; i < num_cmds; i++)
    {
        pids[i] = fork();
        if (pids[i] < 0)
        {
            perror("pipeline fork failed");
            for (int k = 0; k < i; k++)
            {
                kill(pids[k], SIGKILL);
                waitpid(pids[k], NULL, 0);
            }
            free(pipe_fds);
            free(pids);
            return 1;
        }
        else if (pids[i] == 0)
        {
            // child process group setup
            if (isatty(STDIN_FILENO))
            {
                pid_t my_pgid = (pgid == 0) ? getpid() : pgid;
                setpgid(0, my_pgid);
                if (!next_job_bg) // Only claim terminal foreground if NOT background!
                {
                    tcsetpgrp(STDIN_FILENO, my_pgid);
                }
            }

            signal(SIGINT, SIG_DFL);
            signal(SIGQUIT, SIG_DFL);
            signal(SIGTSTP, SIG_DFL);
            signal(SIGTTIN, SIG_DFL);
            signal(SIGTTOU, SIG_DFL);

            // if not the first command, stop stdin from the previous pipe read end
            if (i > 0)
            {
                dup2(pipe_fds[(i - 1) * 2], STDIN_FILENO);
            }
            // if not the last command, stop stdout to the current pipe write end
            if (i < num_cmds - 1)
            {
                dup2(pipe_fds[i * 2 + 1], STDOUT_FILENO);
            }

            //! close all duplicated pipe endpoints in the child
            for (int j = 0; j < 2 * num_pipes; j++)
            {
                close(pipe_fds[j]);
            }

            // tokenize arguments with POSIX backslash/quote parsing
            char *args[MAX_ARGS];
            int argc = 0;
            char *ptr = cmds[i];

            while (*ptr != '\0')
            {
                // Skip whitespace delimiters
                while (*ptr == ' ' || *ptr == '\t' || *ptr == '\r' || *ptr == '\n')
                {
                    ptr++;
                }
                if (*ptr == '\0')
                    break;
                if (argc >= MAX_ARGS - 1)
                    break;

                char *arg = malloc(strlen(ptr) + 1);
                int arg_len = 0;
                int in_double_quote = 0;
                int in_single_quote = 0;
                int can_expand_tilde = 1; // FIXED: Active only at the absolute start of a token

                while (*ptr != '\0')
                {
                    // Handle Tilde Expansion safely outside quotes at token start
                    if (can_expand_tilde && *ptr == '~')
                    {
                        can_expand_tilde = 0;
                        char *curr = ptr + 1;
                        while (*curr != '\0' && *curr != '/' && *curr != ' ' && *curr != '\t' && *curr != '\r' && *curr != '\n')
                        {
                            curr++;
                        }

                        size_t prefix_len = curr - ptr;
                        if (prefix_len == 1) // Standalone "~" or "~/"
                        {
                            char *home = getenv("HOME");
                            if (home != NULL)
                            {
                                strcpy(arg + arg_len, home);
                                arg_len += strlen(home);
                                ptr++;
                                continue;
                            }
                        }
                        else // "~username"
                        {
                            char user[256];
                            size_t u_len = prefix_len - 1;
                            if (u_len > 255)
                            {
                                u_len = 255;
                            }

                            strncpy(user, ptr + 1, u_len);
                            user[u_len] = '\0';

                            struct passwd *pw = getpwnam(user);
                            if (pw != NULL && pw->pw_dir != NULL)
                            {
                                strcpy(arg + arg_len, pw->pw_dir);
                                arg_len += strlen(pw->pw_dir);
                                ptr += prefix_len;
                                continue;
                            }
                        }
                    }

                    // Revoke tilde privilege as soon as any word character flows through
                    can_expand_tilde = 0;

                    if (in_single_quote)
                    {
                        if (*ptr == '\'')
                        {
                            in_single_quote = 0;
                            ptr++;
                        }
                        else
                        {
                            // FIXED: Mask wildcards inside single quotes
                            if (*ptr == '*')
                            {
                                arg[arg_len++] = '\x01';
                            }
                            else if (*ptr == '?')
                            {
                                arg[arg_len++] = '\x02';
                            }
                            else
                            {
                                arg[arg_len++] = *ptr;
                            }
                            ptr++;
                        }
                    }
                    else if (in_double_quote)
                    {
                        if (*ptr == '"')
                        {
                            in_double_quote = 0;
                            ptr++;
                        }
                        else if (*ptr == '\\')
                        {
                            char next = *(ptr + 1);
                            if (next == '"' || next == '$' || next == '\\' || next == '`' || next == '\n')
                            {
                                if (next != '\0')
                                {
                                    if (next == '*')
                                    {
                                        arg[arg_len++] = '\x01';
                                    }
                                    else if (next == '?')
                                    {
                                        arg[arg_len++] = '\x02';
                                    }
                                    else
                                    {
                                        arg[arg_len++] = next;
                                    }
                                    ptr += 2;
                                }
                                else
                                {
                                    arg[arg_len++] = '\\';
                                    ptr++;
                                }
                            }
                            else
                            {
                                arg[arg_len++] = '\\';
                                ptr++;
                            }
                        }
                        else
                        {
                            // FIXED: Mask wildcards inside double quotes
                            if (*ptr == '*')
                            {
                                arg[arg_len++] = '\x01';
                            }
                            else if (*ptr == '?')
                            {
                                arg[arg_len++] = '\x02';
                            }
                            else
                            {
                                arg[arg_len++] = *ptr;
                            }
                            ptr++;
                        }
                    }
                    else
                    {
                        // Outside of any quotes
                        if (*ptr == '\\')
                        {
                            ptr++; // Skip backslash literal
                            if (*ptr != '\0')
                            {
                                // FIXED: Mask backslash-escaped wildcards
                                if (*ptr == '*')
                                {
                                    arg[arg_len++] = '\x01';
                                }
                                else if (*ptr == '?')
                                {
                                    arg[arg_len++] = '\x02';
                                }
                                else
                                {
                                    arg[arg_len++] = *ptr;
                                }
                                ptr++;
                            }
                        }
                        else if (*ptr == '\'')
                        {
                            in_single_quote = 1;
                            ptr++;
                        }
                        else if (*ptr == '"')
                        {
                            in_double_quote = 1;
                            ptr++;
                        }
                        else if (*ptr == ' ' || *ptr == '\t' || *ptr == '\r' || *ptr == '\n')
                        {
                            break; // Unquoted/unescaped whitespace means token end
                        }
                        else
                        {
                            arg[arg_len++] = *ptr;
                            ptr++;
                        }
                    }
                }
                arg[arg_len] = '\0';
                args[argc++] = arg;
            }
            args[argc] = NULL;

            // if '!' is a command name in pipeline, skip it
            if (args[0] != NULL && strcmp(args[0], "!") == 0)
            {
                int k = 0;
                while (args[k] != NULL)
                {
                    args[k] = args[k + 1];
                    k++;
                }
            }

            // count environment variable assignments
            int assign_count = 0;
            while (args[assign_count] != NULL && strchr(args[assign_count], '=') != NULL)
            {
                assign_count++;
            }

            char **actual_args = args;
            if (assign_count > 0)
            {
                for (int j = 0; j < assign_count; j++)
                {
                    char *eq = strchr(args[j], '=');
                    if (eq)
                    {
                        *eq = '\0';
                        setenv(args[j], eq + 1, 1);
                    }
                }
                actual_args = &args[assign_count];
            }

            if (actual_args[0] != NULL)
            {
                // apply globbing to the arguments
                char **globbed_args = apply_globbing(actual_args);

                // handle file redirections for this pipeline stage
                if (check_and_handle_redirections(globbed_args) < 0)
                {
                    exit(1);
                }

                // process commands inside the pipeline subshell sandbox
                if (strcmp(globbed_args[0], "cd") == 0)
                {
                    char *target_dir = globbed_args[1];

                    if (target_dir == NULL)
                    {
                        target_dir = getenv("HOME");
                    }

                    if (chdir(target_dir) != 0)
                    {
                        perror("cd failed");
                    }

                    exit(0);
                }

                // handle built-in commands that should terminate the pipeline child
                if (strcmp(globbed_args[0], "exit") == 0)
                {
                    int exit_code = 0;
                    if (globbed_args[1] != NULL)
                    {
                        exit_code = atoi(globbed_args[1]);
                    }

                    exit(exit_code);
                }

                // handle export command in the pipeline child
                if (strcmp(globbed_args[0], "export") == 0)
                {
                    exit(0);
                }

                // handle exec command in the pipeline child
                if (strcmp(globbed_args[0], "exec") == 0)
                {
                    if (globbed_args[1] == NULL)
                    {
                        exit(0);
                    }

                    execvp(globbed_args[1], &globbed_args[1]);
                    perror("exec failed");
                    exit(1);
                }

                execvp(globbed_args[0], globbed_args);
                perror("execvp failed");
            }

            exit(127);
        }
        else
        {
            // Parent side loop sync: secure child group membership instantly before race wins
            if (pgid == 0)
            {
                pgid = pids[0];
            }
            if (isatty(STDIN_FILENO))
            {
                setpgid(pids[i], pgid);
            }
        }
    }

    // Hand off foreground terminal focus to the running pipeline group
    if (isatty(STDIN_FILENO) && !next_job_bg) // Guard against background lines
    {
        tcsetpgrp(STDIN_FILENO, pgid);
    }

    // close parent-side pipes so EOF signals is returned from children
    for (int j = 0; j < 2 * num_pipes; j++)
    {
        close(pipe_fds[j]);
    }

    // Return instantly for background pipelines without blocking
    if (next_job_bg)
    {
        last_job_pid = pgid;
        free(pipe_fds);
        free(pids);
        return 0;
    }

    // wait for all children to complete and save the exit code of the last command
    int last_exit_code = 0;
    for (int i = 0; i < num_cmds; i++)
    {
        int status;
        waitpid(pids[i], &status, WUNTRACED);

        if (i == num_cmds - 1)
        {
            if (WIFEXITED(status))
            {
                last_exit_code = WEXITSTATUS(status);
            }
            else if (WIFSIGNALED(status))
            {
                last_exit_code = 128 + WTERMSIG(status);
            }
            else if (WIFSTOPPED(status))
            {
                last_job_pid = pgid; // FIXED: Safely tracks pipeline group for fg/bg routines!
                last_exit_code = 128 + WSTOPSIG(status);
            }
            else
            {
                last_exit_code = 1;
            }
        }
    }

    // Safely reclaim foreground terminal control back to the shell prompt
    if (isatty(STDIN_FILENO))
    {
        tcsetpgrp(STDIN_FILENO, getpgrp());
    }

    free(pipe_fds);
    free(pids);
    return last_exit_code;
}
