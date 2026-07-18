#include "shell.h"

// executes a command line with support for command separators, conditional execution, subshells, and pipelines
int execute_command_line(char *line)
{
    char *ptr = line;
    int last_exit_code = 0;
    int condition = 0; // 0 = always run, 1 = run if success (&&), 2 = run if fail (||)

    while (*ptr != '\0')
    {
        // trim leading spaces
        while (*ptr == ' ' || *ptr == '\t' || *ptr == '\r' || *ptr == '\n')
        {
            ptr++;
        }

        if (*ptr == '\0')
        {
            break;
        }

        // handle command separator
        if (*ptr == ';')
        {
            condition = 0;
            ptr++;
            continue;
        }

        // Handle background operator separator
        if (*ptr == '&' && *(ptr + 1) != '&')
        {
            condition = 0; // Next command runs unconditionally
            ptr++;
            continue;
        }

        // handle conditional operators
        if (strncmp(ptr, "&&", 2) == 0)
        {
            condition = 1;
            ptr += 2;
            continue;
        }

        if (strncmp(ptr, "||", 2) == 0)
        {
            condition = 2;
            ptr += 2;
            continue;
        }

        // determine short-circuit status
        int should_execute = 0;
        if (condition == 0)
        {
            should_execute = 1;
        }
        else if (condition == 1 && last_exit_code == 0)
        {
            should_execute = 1;
        }
        else if (condition == 2 && last_exit_code != 0)
        {
            should_execute = 1;
        }

        int negate = 0;
        if (*ptr == '!')
        {
            // in posix the '!' should be a separate word
            if (ptr[1] == ' ' || ptr[1] == '\t' || ptr[1] == '(' || ptr[1] == '\n')
            {
                negate = 1;
                ptr++; // skip '!'

                // clear the space between '!' and command
                while (*ptr == ' ' || *ptr == '\t' || *ptr == '\r' || *ptr == '\n')
                {
                    ptr++;
                }
            }
        }

        //* SUBSHELL
        if (*ptr == '(')
        {
            ptr++; // move past (
            char *subshell_start = ptr;
            int paren_count = 1;

            // scan left to right to find the matching )
            while (*ptr != '\0' && paren_count > 0)
            {
                if (*ptr == '(')
                {
                    paren_count++;
                }
                else if (*ptr == ')')
                {
                    paren_count--;
                }

                if (paren_count > 0)
                {
                    ptr++;
                }
            }

            char *end_paren = ptr;
            if (*ptr == ')')
            {
                ptr++; // step past closing )
            }

            if (should_execute)
            {
                *end_paren = '\0'; // isolate subshell

                pid_t subshell_pid = fork();
                if (subshell_pid < 0)
                {
                    perror("subshell fork failed");
                    last_exit_code = 1;
                }
                else if (subshell_pid == 0)
                {
                    if (isatty(STDIN_FILENO))
                    {
                        setpgid(0, 0);
                        tcsetpgrp(STDIN_FILENO, getpid());
                    }
                    signal(SIGINT, SIG_DFL);
                    signal(SIGQUIT, SIG_DFL);
                    signal(SIGTSTP, SIG_DFL);
                    signal(SIGTTIN, SIG_DFL);
                    signal(SIGTTOU, SIG_DFL);

                    int ret = execute_command_line(subshell_start);
                    exit(ret);
                }
                else
                {
                    // Parent-side subshell Job Control synchronization
                    if (isatty(STDIN_FILENO))
                    {
                        setpgid(subshell_pid, subshell_pid);
                        tcsetpgrp(STDIN_FILENO, subshell_pid);
                    }

                    int status;
                    waitpid(subshell_pid, &status, WUNTRACED);

                    // Instantly reclaim foreground terminal focus
                    if (isatty(STDIN_FILENO))
                    {
                        tcsetpgrp(STDIN_FILENO, getpgrp());
                    }

                    int exec_result = 1;
                    if (WIFEXITED(status))
                    {
                        exec_result = WEXITSTATUS(status);
                    }
                    else if (WIFSIGNALED(status))
                    {
                        exec_result = 128 + WTERMSIG(status);
                    }
                    else if (WIFSTOPPED(status))
                    {
                        last_job_pid = subshell_pid; // Track subshell group if suspended
                        exec_result = 128 + WSTOPSIG(status);
                    }

                    last_exit_code = exec_result;

                    if (negate == 1)
                    {
                        last_exit_code = (exec_result == 0) ? 1 : 0;
                    }
                }
            }

            continue;
        }

        //* REGULAR
        // find the boundary of this command block
        char *end = ptr;
        int is_background = 0; // Local background detector

        while (*end != '\0' && *end != ';' && strncmp(end, "&&", 2) != 0 && strncmp(end, "||", 2) != 0 && *end != '(')
        {
            // Detect standalone background operator
            if (*end == '&' && *(end + 1) != '&')
            {
                if (end == ptr || (*(end - 1) != '<' && *(end - 1) != '>'))
                {
                    is_background = 1;
                    break;
                }
            }

            // skip the contents of $(...) completely
            if (strncmp(end, "$(", 2) == 0)
            {
                end += 2;
                int depth = 1;
                while (*end != '\0' && depth > 0)
                {
                    if (strncmp(end, "$(", 2) == 0)
                    {
                        depth++;
                        end += 2;
                    }
                    else if (*end == ')')
                    {
                        depth--;
                        end++;
                    }
                    else
                    {
                        end++;
                    }
                }

                continue; // resume scanning past the matching execution boundary
            }

            end++;
        }

        char saved_char = *end;
        *end = '\0'; // isolate the command or pipeline

        if (should_execute)
        {
            next_job_bg = is_background; // Set the global background routing flag

            // Transform any inner command substitutions first
            char *sub_expanded = expand_all_substitutions(ptr);

            // Chain the environment variable expansion pass right after it
            char *expanded_ptr = expand_environment_variables(sub_expanded, last_exit_code);
            free(sub_expanded); // Clean up the intermediate buffer

            // scan the command for a pipe before splitting arguments
            if (strchr(expanded_ptr, '|') != NULL)
            {
                int pipe_result = execute_pipeline(expanded_ptr);
                if (negate == 1)
                {
                    last_exit_code = !pipe_result;
                }
                else
                {
                    last_exit_code = pipe_result;
                }
            }
            else
            {
                // standard single command execution with POSIX backslash/quote parsing
                char *args[MAX_ARGS];
                int i = 0;
                char *arg_ptr = expanded_ptr;

                while (*arg_ptr != '\0')
                {
                    // Skip whitespace delimiters
                    while (*arg_ptr == ' ' || *arg_ptr == '\t' || *arg_ptr == '\r' || *arg_ptr == '\n')
                    {
                        arg_ptr++;
                    }

                    if (*arg_ptr == '\0')
                    {
                        break;
                    }

                    if (i >= MAX_ARGS - 1)
                    {
                        break;
                    }

                    // Allocate a buffer to build the quote-stripped argument
                    char *arg = malloc(strlen(arg_ptr) + 1);
                    int arg_len = 0;
                    int in_double_quote = 0;
                    int in_single_quote = 0;
                    int can_expand_tilde = 1; // FIXED: Active only at the absolute start of a token

                    while (*arg_ptr != '\0')
                    {
                        // Handle Tilde Expansion safely outside quotes at token start
                        if (can_expand_tilde && *arg_ptr == '~')
                        {
                            can_expand_tilde = 0;
                            char *curr = arg_ptr + 1;
                            while (*curr != '\0' && *curr != '/' && *curr != ' ' && *curr != '\t' && *curr != '\r' && *curr != '\n')
                            {
                                curr++;
                            }
                            size_t prefix_len = curr - arg_ptr;
                            if (prefix_len == 1) // Standalone "~" or "~/"
                            {
                                char *home = getenv("HOME");
                                if (home != NULL)
                                {
                                    strcpy(arg + arg_len, home);
                                    arg_len += strlen(home);
                                    arg_ptr++;
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

                                strncpy(user, arg_ptr + 1, u_len);
                                user[u_len] = '\0';

                                struct passwd *pw = getpwnam(user);
                                if (pw != NULL && pw->pw_dir != NULL)
                                {
                                    strcpy(arg + arg_len, pw->pw_dir);
                                    arg_len += strlen(pw->pw_dir);
                                    arg_ptr += prefix_len;
                                    continue;
                                }
                            }
                        }

                        // Revoke tilde privilege as soon as any word character flows through
                        can_expand_tilde = 0;

                        if (in_single_quote)
                        {
                            if (*arg_ptr == '\'')
                            {
                                in_single_quote = 0;
                                arg_ptr++;
                            }
                            else
                            {
                                // FIXED: Mask wildcards inside single quotes
                                if (*arg_ptr == '*')
                                    arg[arg_len++] = '\x01';
                                else if (*arg_ptr == '?')
                                    arg[arg_len++] = '\x02';
                                else
                                    arg[arg_len++] = *arg_ptr;
                                arg_ptr++;
                            }
                        }
                        else if (in_double_quote)
                        {
                            if (*arg_ptr == '"')
                            {
                                in_double_quote = 0;
                                arg_ptr++;
                            }
                            else if (*arg_ptr == '\\')
                            {
                                char next = *(arg_ptr + 1);
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
                                        arg_ptr += 2;
                                    }
                                    else
                                    {
                                        arg[arg_len++] = '\\';
                                        arg_ptr++;
                                    }
                                }
                                else
                                {
                                    arg[arg_len++] = '\\';
                                    arg_ptr++;
                                }
                            }
                            else
                            {
                                // FIXED: Mask wildcards inside double quotes
                                if (*arg_ptr == '*')
                                {
                                    arg[arg_len++] = '\x01';
                                }
                                else if (*arg_ptr == '?')
                                {
                                    arg[arg_len++] = '\x02';
                                }
                                else
                                {
                                    arg[arg_len++] = *arg_ptr;
                                }
                                arg_ptr++;
                            }
                        }
                        else
                        {
                            // Outside of any quotes
                            if (*arg_ptr == '\\')
                            {
                                arg_ptr++; // Skip the backslash literal
                                if (*arg_ptr != '\0')
                                {
                                    // FIXED: Mask backslash-escaped wildcards
                                    if (*arg_ptr == '*')
                                    {
                                        arg[arg_len++] = '\x01';
                                    }
                                    else if (*arg_ptr == '?')
                                    {
                                        arg[arg_len++] = '\x02';
                                    }
                                    else
                                    {
                                        arg[arg_len++] = *arg_ptr;
                                    }
                                    arg_ptr++;
                                }
                            }
                            else if (*arg_ptr == '\'')
                            {
                                in_single_quote = 1;
                                arg_ptr++;
                            }
                            else if (*arg_ptr == '"')
                            {
                                in_double_quote = 1;
                                arg_ptr++;
                            }
                            else if (*arg_ptr == ' ' || *arg_ptr == '\t' || *arg_ptr == '\r' || *arg_ptr == '\n')
                            {
                                break; // Unquoted/unescaped whitespace means token end
                            }
                            else
                            {
                                arg[arg_len++] = *arg_ptr;
                                arg_ptr++;
                            }
                        }
                    }
                    arg[arg_len] = '\0';
                    args[i++] = arg;
                }
                args[i] = NULL;

                if (args[0] != NULL)
                {
                    char **globbed_args = apply_globbing(args);

                    int exec_result = execute_single_command(globbed_args);

                    if (negate == 1)
                    {
                        last_exit_code = !exec_result;
                    }
                    else
                    {
                        last_exit_code = exec_result;
                    }

                    for (int k = 0; globbed_args[k] != NULL; k++)
                    {
                        free(globbed_args[k]);
                    }

                    free(globbed_args);
                }

                // Clean up dynamically allocated argument strings
                for (int k = 0; k < i; k++)
                {
                    free(args[k]);
                }
            }

            free(expanded_ptr);
            next_job_bg = 0; // Reset global state flag after invocation pass
        }

        *end = saved_char;
        ptr = end; // move pointer forward
    }

    last_exit_status = last_exit_code; // Sync exit code globally for heredocs
    return last_exit_code;
}
