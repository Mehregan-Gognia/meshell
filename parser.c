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
            // in posix the '!' should be a seperate word
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
                    // child: recursively execute inside the sandbox, returning the final status
                    int ret = execute_command_line(subshell_start);
                    exit(ret);
                }
                else
                {
                    // parent: wait and capture the status of the subshell sandbox
                    int status;
                    waitpid(subshell_pid, &status, 0);

                    int exec_result = 1;
                    if (WIFEXITED(status))
                    {
                        exec_result = WEXITSTATUS(status);
                    }

                    if (negate)
                    {
                        last_exit_code = (exec_result == 0) ? 1 : 0;
                    }
                    else
                    {
                        last_exit_code = exec_result;
                    }
                }
            }

            continue;
        }

        //* REGULAR
        // find the boundary of this command block
        char *end = ptr;
        while (*end != '\0' && *end != ';' && strncmp(end, "&&", 2) != 0 && strncmp(end, "||", 2) != 0 && *end != '(')
        {
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
                char *args[100];
                int i = 0;
                char *arg_save_ptr;
                char *token = strtok_r(ptr, " \t\r\n", &arg_save_ptr);
                while (token != NULL)
                {
                    if (i >= 99)
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
        }

        *end = saved_char;
        ptr = end; // move pointer forward
    }

    return last_exit_code;
}
