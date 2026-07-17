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
    }

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