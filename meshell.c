#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

int execute_single_command(char **args)
{
    //* EXEC
    if (strcmp(args[0], "exec") == 0)
    {
        if (args[1] == NULL)
        {
            exit(0);
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

    //* FORK
    pid_t pid = fork();
    if (pid < 0)
    {
        perror("fork failed");
        return 1;
    }
    else if (pid == 0)
    {
        if (execvp(args[0], args) == -1)
        {
            perror("execvp failed");
            exit(127);
        }
    }
    else
    {
        int status;
        waitpid(pid, &status, 0);

        // extract the exit code of the child process
        if (WIFEXITED(status))
        {
            return WEXITSTATUS(status);
        }
    }

    return 1;
}

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
            end++;
        }

        char saved_char = *end;
        *end = '\0'; // isolate the command chunk

        if (should_execute)
        {
            char *args[100];
            int i = 0;
            char *arg_save_ptr;
            char *token = strtok_r(ptr, " \t\r\n", &arg_save_ptr);
            while (token != NULL)
            {
                args[i++] = token;
                token = strtok_r(NULL, " \t\r\n", &arg_save_ptr);
            }
            args[i] = NULL;

            if (args[0] != NULL)
            {
                int exec_result = execute_single_command(args);

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

        *end = saved_char;
        ptr = end; // move pointer forward
    }
    return last_exit_code;
}

int main()
{
    char *buffer = NULL;
    size_t buffer_size = 0;
    ssize_t characters_read;

    while (1)
    {
        printf("$ ");
        fflush(stdout);

        char *complete_line = NULL;
        size_t complete_alloc = 0;
        int keep_reading = 1;

        while (keep_reading)
        {
            characters_read = getline(&buffer, &buffer_size, stdin);
            if (characters_read == -1)
            {
                break;
            }

            // strip trailing newlines
            size_t len = strlen(buffer);
            while (len > 0 && (buffer[len - 1] == '\n' || buffer[len - 1] == '\r'))
            {
                buffer[len - 1] = '\0';
                len--;
            }

            // check if line ends with a backslash
            int has_backslash = 0;
            if (len > 0 && buffer[len - 1] == '\\')
            {
                has_backslash = 1;
                buffer[len - 1] = '\0'; // drpo the backslash
                len--;
            }

            // dynamically grow the command string
            if (complete_line == NULL)
            {
                complete_alloc = len + 1;
                complete_line = malloc(complete_alloc);
                strcpy(complete_line, buffer);
            }
            else
            {
                size_t old_len = strlen(complete_line);
                complete_alloc = old_len + len + 1;
                complete_line = realloc(complete_line, complete_alloc);
                strcat(complete_line, buffer);
            }

            if (!has_backslash)
            {
                keep_reading = 0;
            }
        }

        // handle exit on EOF (ctrl+D)
        if (characters_read == -1 && (complete_line == NULL || strlen(complete_line) == 0))
        {
            if (complete_line)
            {
                free(complete_line);
            }
            break;
        }

        // send the reassembled multi line command
        if (complete_line != NULL)
        {
            execute_command_line(complete_line);
            free(complete_line);
        }
    }
    free(buffer);
    return 0;
}