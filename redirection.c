#include "shell.h"

// redirection operators: <, >, >>, 2>, 2>>, <>, 1<>, <&, >&
// dynamically extracts file descriptor prefixes and handles descriptor closure <&- / duplication 2>&1
// returns 0 on success, -1 on failure
int check_and_handle_redirections(char **args)
{
    int i = 0;
    while (args[i] != NULL)
    {
        char *token = args[i];
        int fd_to_replace = -1;
        int flags = 0;
        int is_input = 0;
        int is_dup_closed = 0;
        int is_heredoc = 0;
        char *operator_ptr = token;
        int explicit_fd = -1;

        // check for an explicit file descriptor prefix (0-9)
        if (token[0] >= '0' && token[0] <= '9')
        {
            if (token[1] == '>' || token[1] == '<')
            {
                explicit_fd = token[0] - '0';
                operator_ptr = token + 1;
            }
        }

        int operator_len = 0;

        if (strncmp(operator_ptr, ">>", 2) == 0)
        {
            operator_len = 2;
            flags = O_WRONLY | O_CREAT | O_APPEND;
            fd_to_replace = (explicit_fd != -1) ? explicit_fd : STDOUT_FILENO;
        }
        else if (strncmp(operator_ptr, "<<", 2) == 0)
        {
            operator_len = 2;
            is_heredoc = 1;
            fd_to_replace = (explicit_fd != -1) ? explicit_fd : STDIN_FILENO;
        }
        else if (strncmp(operator_ptr, "<>", 2) == 0)
        {
            operator_len = 2;
            flags = O_RDWR | O_CREAT;
            fd_to_replace = (explicit_fd != -1) ? explicit_fd : STDIN_FILENO;
        }
        else if (strncmp(operator_ptr, "<&", 2) == 0)
        {
            operator_len = 2;
            is_dup_closed = 1;
            fd_to_replace = (explicit_fd != -1) ? explicit_fd : STDIN_FILENO;
        }
        else if (strncmp(operator_ptr, ">&", 2) == 0)
        {
            operator_len = 2;
            is_dup_closed = 1;
            fd_to_replace = (explicit_fd != -1) ? explicit_fd : STDOUT_FILENO;
        }
        else if (operator_ptr[0] == '>')
        {
            operator_len = 1;
            flags = O_WRONLY | O_CREAT | O_TRUNC;
            fd_to_replace = (explicit_fd != -1) ? explicit_fd : STDOUT_FILENO;
        }
        else if (operator_ptr[0] == '<')
        {
            operator_len = 1;
            flags = O_RDONLY;
            is_input = 1;
            fd_to_replace = (explicit_fd != -1) ? explicit_fd : STDIN_FILENO;
        }

        if (fd_to_replace != -1)
        {
            char *filename = NULL;
            int shift_count = 0;

            if (operator_ptr[operator_len] != '\0')
            {
                filename = operator_ptr + operator_len;
                shift_count = 1;
            }
            else
            {
                filename = args[i + 1];
                shift_count = 2;
            }

            if (filename == NULL)
            {
                fprintf(stderr, "no file specified for redirection\n");
                return -1;
            }

            if (is_dup_closed)
            {
                if (strcmp(filename, "-") == 0)
                {
                    close(fd_to_replace);
                }
                else
                {
                    int is_digit = 1;
                    for (int k = 0; filename[k] != '\0'; k++)
                    {
                        if (filename[k] < '0' || filename[k] > '9')
                        {
                            is_digit = 0;
                            break;
                        }
                    }

                    if (is_digit == 0)
                    {
                        fprintf(stderr, "redirection syntax error: expected file descriptor\n");
                        return -1;
                    }

                    int from_fd = atoi(filename);
                    if (from_fd != fd_to_replace)
                    {
                        dup2(from_fd, fd_to_replace);
                    }
                }
            }
            else if (is_heredoc)
            {
                // Strip quotes from delimiter if present; disable expansion if quoted
                int expand = 1;
                char clean_delim[256];
                int d_idx = 0;
                for (int k = 0; filename[k] != '\0'; k++)
                {
                    if (filename[k] == '\'' || filename[k] == '"')
                    {
                        expand = 0;
                    }
                    else
                    {
                        if (d_idx < 255)
                            clean_delim[d_idx++] = filename[k];
                    }
                }
                clean_delim[d_idx] = '\0';

                int heredoc_pipe[2];
                if (pipe(heredoc_pipe) < 0)
                {
                    perror("heredoc pipe failed");
                    return -1;
                }

                char *line_buf = NULL;
                size_t line_cap = 0;
                ssize_t line_len;

                while (1)
                {
                    if (isatty(STDIN_FILENO))
                    {
                        char *rl = readline("> ");
                        if (!rl)
                            break;
                        line_len = strlen(rl);
                        line_buf = realloc(line_buf, line_len + 2);
                        strcpy(line_buf, rl);
                        line_buf[line_len] = '\n';
                        line_buf[line_len + 1] = '\0';
                        line_len++;
                        free(rl);
                    }
                    else
                    {
                        // Use getline on stdin to consume from the same buffer as main.c
                        line_len = getline(&line_buf, &line_cap, stdin);
                        if (line_len == -1)
                            break;
                    }

                    // Strip newlines to cleanly check for the EOF delimiter match
                    char *chk = strdup(line_buf);
                    size_t clen = strlen(chk);
                    while (clen > 0 && (chk[clen - 1] == '\n' || chk[clen - 1] == '\r'))
                    {
                        chk[clen - 1] = '\0';
                        clen--;
                    }

                    if (strcmp(chk, clean_delim) == 0)
                    {
                        free(chk);
                        break;
                    }
                    free(chk);

                    if (expand)
                    {
                        char *expanded = expand_environment_variables(line_buf, last_exit_status);
                        write(heredoc_pipe[1], expanded, strlen(expanded));
                        free(expanded);
                    }
                    else
                    {
                        write(heredoc_pipe[1], line_buf, line_len);
                    }
                }
                free(line_buf);
                close(heredoc_pipe[1]);

                dup2(heredoc_pipe[0], fd_to_replace);
                close(heredoc_pipe[0]);
            }
            else
            {
                int fd;
                if (is_input == 1)
                {
                    fd = open(filename, flags);
                }
                else
                {
                    fd = open(filename, flags, 0644);
                }

                if (fd < 0)
                {
                    perror("open redirection file failed");
                    return -1;
                }

                if (fd != fd_to_replace)
                {
                    dup2(fd, fd_to_replace);
                    close(fd);
                }
            }

            // shift argument vector left to erase redirection tokens
            int j = i;
            while (args[j + shift_count] != NULL)
            {
                args[j] = args[j + shift_count];
                j++;
            }
            args[j] = NULL;

            continue;
        }

        i++;
    }

    return 0;
}
