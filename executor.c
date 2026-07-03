#include "shell.h"

// executes a single command with arguments, handling built in commands and forking for external commands
int execute_single_command(char **args)
{
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

    //* FORK
    pid_t pid = fork();
    if (pid < 0)
    {
        perror("fork failed");
        return 1;
    }
    else if (pid == 0)
    {
        // handle file redirections inside the child
        if (check_and_handle_redirections(args) < 0)
        {
            exit(1);
        }

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

// executes a pipeline of commands separated by the pipe operator '|'
int execute_pipeline(char *line)
{
    char *cmds[100];
    int num_cmds = 0;
    char *pipe_save_ptr;

    char *token = strtok_r(line, "|", &pipe_save_ptr); // break down to seperate commands
    while (token != NULL)
    {
        if (num_cmds >= 99) // prevent stack overflow
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

    for (int i = 0; i < num_cmds; i++)
    {
        pids[i] = fork();
        if (pids[i] < 0)
        {
            perror("pipeline fork failed");
            free(pipe_fds);
            free(pids);
            return 1;
        }
        else if (pids[i] == 0) // child
        {
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

            // tokenize arguments
            char *args[100];
            int argc = 0;
            char *arg_save_ptr;
            char *arg_token = strtok_r(cmds[i], " \t\r\n", &arg_save_ptr);
            while (arg_token != NULL)
            {
                if (argc >= 99) // prevent stack overflow
                {
                    break;
                }

                args[argc++] = arg_token;
                arg_token = strtok_r(NULL, " \t\r\n", &arg_save_ptr);
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

            if (args[0] != NULL)
            {
                // handle file redirections for this pipeline stage
                if (check_and_handle_redirections(args) < 0)
                {
                    exit(1);
                }

                // process commands inside the pipeline subshell sandbox
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
                    }

                    exit(0);
                }

                if (strcmp(args[0], "exit") == 0)
                {
                    int exit_code = 0;
                    if (args[1] != NULL)
                    {
                        exit_code = atoi(args[1]);
                    }

                    exit(exit_code);
                }

                if (strcmp(args[0], "exec") == 0)
                {
                    if (args[1] == NULL)
                    {
                        exit(0);
                    }

                    execvp(args[1], &args[1]);
                    perror("exec failed");
                    exit(1);
                }

                execvp(args[0], args);
                perror("execvp failed");
            }

            exit(127);
        }
    }

    // parent
    // close parent-side pipes so EOF signals is returned from children
    for (int j = 0; j < 2 * num_pipes; j++)
    {
        close(pipe_fds[j]);
    }

    // wait for all children to complete and save the exit code of the last command
    int last_exit_code = 0;
    for (int i = 0; i < num_cmds; i++)
    {
        int status;
        waitpid(pids[i], &status, 0);
        if (i == num_cmds - 1)
        {
            if (WIFEXITED(status))
            {
                last_exit_code = WEXITSTATUS(status);
            }
            else
            {
                last_exit_code = 1;
            }
        }
    }

    free(pipe_fds);
    free(pids);
    return last_exit_code;
}
