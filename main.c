#include "shell.h"

int check_and_handle_redirections(char **args);
int execute_single_command(char **args);
int execute_pipeline(char *line);
char *capture_command_output(char *cmd);
char *expand_all_substitutions(char *src);
int execute_command_line(char *line);

int job_control_enabled = 0;
pid_t last_job_pid = 0;

int main()
{
    int shell_terminal = STDIN_FILENO;
    if (isatty(shell_terminal))
    {
        // Loop until the shell is in the foreground group
        while (tcgetpgrp(shell_terminal) != getpgrp())
        {
            kill(-getpgrp(), SIGTTIN);
        }

        // Ignore interactive job control signals in the parent shell
        signal(SIGINT, SIG_IGN);
        signal(SIGQUIT, SIG_IGN);
        signal(SIGTSTP, SIG_IGN);
        signal(SIGTTIN, SIG_IGN);
        signal(SIGTTOU, SIG_IGN);

        // Secure the shell inside its own process group
        pid_t shell_pgid = getpid();
        setpgid(shell_pgid, shell_pgid);
        tcsetpgrp(shell_terminal, shell_pgid);

        // Initialize custom Readline completion hooks
        init_completion();
    }

    while (1)
    {
        char *complete_line = NULL;
        size_t complete_alloc = 0;
        int keep_reading = 1;
        const char *prompt = "$ ";

        while (keep_reading)
        {
            char *line = NULL;

            if (isatty(STDIN_FILENO))
            {
                line = readline(prompt);
            }
            else
            {
                char *buf = NULL;
                size_t buf_size = 0;
                ssize_t read_bytes = getline(&buf, &buf_size, stdin);
                if (read_bytes != -1)
                {
                    line = buf;
                    size_t l = strlen(line);
                    while (l > 0 && (line[l - 1] == '\n' || line[l - 1] == '\r'))
                    {
                        line[l - 1] = '\0';
                        l--;
                    }
                }
                else
                {
                    free(buf);
                    line = NULL;
                }
            }

            if (line == NULL)
            {
                keep_reading = 0;
                break;
            }

            // Strip trailing continuation backslash
            size_t len = strlen(line);
            int has_backslash = 0;
            if (len > 0 && line[len - 1] == '\\')
            {
                has_backslash = 1;
                line[len - 1] = '\0';
                len--;
            }

            if (complete_line == NULL)
            {
                complete_alloc = len + 1;
                complete_line = malloc(complete_alloc);
                strcpy(complete_line, line);
            }
            else
            {
                size_t old_len = strlen(complete_line);
                complete_alloc = old_len + len + 1;
                complete_line = realloc(complete_line, complete_alloc);
                strcat(complete_line, line);
            }

            free(line);

            if (has_backslash)
            {
                prompt = "> ";
            }
            else
            {
                keep_reading = 0;
            }
        }

        // Handle exit on EOF (Ctrl+D)
        if (complete_line == NULL)
        {
            break;
        }

        if (strlen(complete_line) > 0)
        {
            if (isatty(STDIN_FILENO))
            {
                add_history(complete_line);
            }
            execute_command_line(complete_line);
        }

        free(complete_line);
    }

    return 0;
}