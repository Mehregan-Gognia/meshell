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

    // EXIT
    if (strcmp(args[0], "exit") == 0)
    {
        exit(0);
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

int main()
{
    char *buffer = NULL;
    size_t buffer_size = 0;
    ssize_t characters_read;

    while (1)
    {
        printf("$ ");
        fflush(stdout);

        characters_read = getline(&buffer, &buffer_size, stdin);

        if (characters_read == -1)
        {
            break;
        }
        buffer[strcspn(buffer, "\r\n")] = '\0';

        // pointers for strtok_r
        char *cmd_save_ptr;
        char *arg_save_ptr;

        // outer loop: spliting the main buffer
        char *command = strtok_r(buffer, ";", &cmd_save_ptr);
        while (command != NULL)
        {
            char *args[100];
            int i = 0;

            int last_command_success = 1; // 1 = true/success, 0 = false/failure
            int condition = 0;            // 0 = none, 1 = run if success (&&), 2 = run if fail (||)

            char *token = strtok_r(command, " \t\r\n", &arg_save_ptr);

            // runs as long as there are tokens to read, or if there's a final command left to run
            while (token != NULL || i > 0)
            {
                // if it's a regular argument, save it and advance
                if (token != NULL && strcmp(token, "&&") != 0 && strcmp(token, "||") != 0)
                {
                    args[i++] = token;
                    token = strtok_r(NULL, " \t\r\n", &arg_save_ptr);
                    continue;
                }

                // we hit an operator (&& / ||) or reached the end of the tokens (NULL)
                args[i] = NULL;

                if (i > 0)
                {
                    // check if short circuit rules allow us to run this block
                    int should_execute = 0;
                    if (condition == 0)
                        should_execute = 1;
                    else if (condition == 1 && last_command_success)
                        should_execute = 1;
                    else if (condition == 2 && !last_command_success)
                        should_execute = 1;

                    if (should_execute)
                    {
                        int exit_code = execute_single_command(args);
                        last_command_success = (exit_code == 0);
                    }
                    i = 0; // reset argument index
                }

                if (token == NULL)
                {
                    break; // no more tokens in this semicolon block
                }

                // update the condition state rule for the next command
                if (strcmp(token, "&&") == 0)
                {
                    condition = 1;
                }
                else if (strcmp(token, "||") == 0)
                {
                    condition = 2;
                }

                token = strtok_r(NULL, " \t\r\n", &arg_save_ptr);
            }

            // move to next semi colon command
            command = strtok_r(NULL, ";", &cmd_save_ptr);
        }
    }

    free(buffer);
    return 0;
}