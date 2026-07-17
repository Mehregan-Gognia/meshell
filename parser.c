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
            last_exit_code = 0; // reset for next commands
            ptr++;
            continue;
        }

        // Handle background operator separator
        if (*ptr == '&' && *(ptr + 1) != '&')
        {
            condition = 0; // Next command runs unconditionally
            last_exit_code = 0;
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
                    // FIXED: Parent-side subshell Job Control synchronization
                    if (isatty(STDIN_FILENO))
                    {
                        setpgid(subshell_pid, subshell_pid);
                        tcsetpgrp(STDIN_FILENO, subshell_pid);
                    }

                    int status;
                    waitpid(subshell_pid, &status, WUNTRACED);

                    // FIXED: Instantly reclaim foreground terminal focus
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

            // transform any inner command substitutions before splitting arguments
            char *expanded_ptr = expand_all_substitutions(ptr);

            // scan the command for a pipe before splitting arguments
            if (strchr(expanded_ptr, '|') != NULL)
            {
                int pipe_result = execute_pipeline(expanded_ptr);
                if (negate)
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
                // standard single command execution
                char *args[MAX_ARGS];
                int i = 0;
                char *arg_save_ptr;

                // Changed 'ptr' to 'expanded_ptr' to capture command substitution args!
                char *token = strtok_r(expanded_ptr, " \t\r\n", &arg_save_ptr);
                while (token != NULL)
                {
                    if (i >= MAX_ARGS - 1) // prevent stack overflow
                    {
                        break; // hard bounds check
                    }
                    args[i++] = token;
                    token = strtok_r(NULL, " \t\r\n", &arg_save_ptr);
                }
                args[i] = NULL;

                if (args[0] != NULL)
                {
                    int exec_result = execute_single_command(args);

                    if (negate)
                    {
                        last_exit_code = !exec_result;
                    }
                    else
                    {
                        last_exit_code = exec_result;
                    }
                }
            }

            free(expanded_ptr);
            next_job_bg = 0; // Reset global state flag after invocation pass
        }

        *end = saved_char;
        ptr = end; // move pointer forward
    }

    return last_exit_code;
}
