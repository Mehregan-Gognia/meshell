#include "shell.h"

int next_job_bg = 0; // Added: Global background state definition

// executes a single command with arguments, handling built in commands and forking for external commands
int execute_single_command(char **args)
{
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
        // child
        if (isatty(STDIN_FILENO))
        {
            setpgid(0, 0);
            if (!next_job_bg) //! Only claim terminal foreground if NOT a background job
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
    char *cmds[100];
    int num_cmds = 0;
    char *pipe_save_ptr;

    char *token = strtok_r(line, "|", &pipe_save_ptr); // break down to separate commands
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
    pid_t pgid = 0;

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
