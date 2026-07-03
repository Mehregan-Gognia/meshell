#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

int check_and_handle_redirections(char **args);
int execute_single_command(char **args);
int execute_pipeline(char *line);
char *capture_command_output(char *cmd);
char *expand_all_substitutions(char *src);
int execute_command_line(char *line);

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
        char *operator_ptr = token;
        int explicit_fd = -1;

        // check for an explicit file descriptor prefix (0-9)
        if (token[0] >= '0' && token[0] <= '9')
        {
            // ensure the digit is immediately followed by a redirection operator
            if (token[1] == '>' || token[1] == '<')
            {
                explicit_fd = token[0] - '0';
                operator_ptr = token + 1; // slide operator pointer past the digit
            }
        }

        int operator_len = 0;

        // identify the operator relative to our adjusted pointer position
        if (strncmp(operator_ptr, ">>", 2) == 0)
        {
            operator_len = 2;
            flags = O_WRONLY | O_CREAT | O_APPEND;
            if (explicit_fd != -1)
            {
                fd_to_replace = explicit_fd;
            }
            else
            {
                fd_to_replace = STDOUT_FILENO;
            }
        }
        else if (strncmp(operator_ptr, "<>", 2) == 0)
        {
            operator_len = 2;
            flags = O_RDWR | O_CREAT;
            if (explicit_fd != -1)
            {
                fd_to_replace = explicit_fd;
            }
            else
            {
                fd_to_replace = STDIN_FILENO;
            }
        }
        else if (strncmp(operator_ptr, "<&", 2) == 0) // input file descriptor duplication/closure
        {
            operator_len = 2;
            is_dup_closed = 1;
            if (explicit_fd != -1)
            {
                fd_to_replace = explicit_fd;
            }
            else
            {
                fd_to_replace = STDIN_FILENO;
            }
        }
        else if (strncmp(operator_ptr, ">&", 2) == 0) // output file descriptor duplication/closure
        {
            operator_len = 2;
            is_dup_closed = 1;
            if (explicit_fd != -1)
            {
                fd_to_replace = explicit_fd;
            }
            else
            {
                fd_to_replace = STDOUT_FILENO;
            }
        }
        else if (operator_ptr[0] == '>')
        {
            operator_len = 1;
            flags = O_WRONLY | O_CREAT | O_TRUNC;
            if (explicit_fd != -1)
            {
                fd_to_replace = explicit_fd;
            }
            else
            {
                fd_to_replace = STDOUT_FILENO;
            }
        }
        else if (operator_ptr[0] == '<')
        {
            operator_len = 1;
            flags = O_RDONLY;
            is_input = 1;
            if (explicit_fd != -1)
            {
                fd_to_replace = explicit_fd;
            }
            else
            {
                fd_to_replace = STDIN_FILENO;
            }
        }

        // process the redirection if an operator was matched
        if (fd_to_replace != -1)
        {
            char *filename = NULL;
            int shift_count = 0;

            // track whether the filename is attached or standalone
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
                    int from_fd = atoi(filename);
                    if (from_fd != fd_to_replace)
                    {
                        dup2(from_fd, fd_to_replace);
                    }
                }
            }
            else
            {
                int fd;
                if (is_input)
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

                // duplicate the file descriptor into our target slot
                if (fd != fd_to_replace)
                {
                    dup2(fd, fd_to_replace);
                    close(fd);
                }
            }

            // shift argument vector left to completely erase the redirection tokens
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

char *capture_command_output(char *cmd)
{
    int pipefds[2];
    if (pipe(pipefds) < 0)
    {
        perror("substitution pipe failed");
        return strdup("");
    }

    pid_t pid = fork();
    if (pid < 0)
    {
        perror("substitution fork failed");
        close(pipefds[0]);
        close(pipefds[1]);
        return strdup("");
    }

    if (pid == 0)
    {
        // child: stdout to the write end
        dup2(pipefds[1], STDOUT_FILENO);
        close(pipefds[0]);
        close(pipefds[1]);

        // evaluate inner shell command
        int ret = execute_command_line(cmd);
        exit(ret);
    }

    // parent
    close(pipefds[1]);

    size_t cap = 4096;
    size_t len = 0;
    char *output = malloc(cap);
    output[0] = '\0';

    char read_buf[256];
    ssize_t bytes_read;
    while ((bytes_read = read(pipefds[0], read_buf, sizeof(read_buf))) > 0)
    {
        if (len + bytes_read >= cap)
        {
            cap *= 2;
            output = realloc(output, cap);
        }
        memcpy(output + len, read_buf, bytes_read);
        len += bytes_read;
        output[len] = '\0';
    }
    close(pipefds[0]);
    waitpid(pid, NULL, 0);

    // strip trailing newlines returns
    while (len > 0 && (output[len - 1] == '\n' || output[len - 1] == '\r'))
    {
        output[len - 1] = '\0';
        len--;
    }

    return output;
}

// parsing a string left to right, processing any $(...) blocks
char *expand_all_substitutions(char *src)
{
    char *ptr = src;
    char *start = strstr(ptr, "$(");
    if (start == NULL)
    {
        return strdup(src);
    }

    size_t result_cap = strlen(src) + 512;
    char *result = malloc(result_cap);
    result[0] = '\0';
    size_t result_len = 0;

    while (start != NULL)
    {
        // append everything to the $(
        size_t prefix_len = start - ptr;
        if (result_len + prefix_len >= result_cap)
        {
            result_cap += prefix_len + 512;
            result = realloc(result, result_cap);
        }

        strncat(result, ptr, prefix_len);
        result_len += prefix_len;

        // scan to find closing parenthesis
        char *sub_ptr = start + 2;
        int depth = 1;
        while (*sub_ptr != '\0' && depth > 0)
        {
            if (strncmp(sub_ptr, "$(", 2) == 0)
            {
                depth++;
                sub_ptr += 2;
            }
            else if (*sub_ptr == ')')
            {
                depth--;
                if (depth > 0)
                    sub_ptr++;
            }
            else
            {
                sub_ptr++;
            }
        }

        if (*sub_ptr == ')')
        {
            *sub_ptr = '\0'; // isolate inner command
            char *inner_cmd = start + 2;

            // recursively expand any nested substitutions inside the inner command block first
            char *expanded_inner = expand_all_substitutions(inner_cmd);
            char *output = capture_command_output(expanded_inner);
            free(expanded_inner);

            // splice the captured stdout into result builder
            size_t output_len = strlen(output);
            if (result_len + output_len >= result_cap)
            {
                result_cap += output_len + 512;
                result = realloc(result, result_cap);
            }

            strcat(result, output);
            result_len += output_len;
            free(output);

            ptr = sub_ptr + 1; // advance past the closing ')'
        }
        else
        {
            // fallback for unmatched expressions
            if (result_len + 2 >= result_cap)
            {
                result_cap += 512;
                result = realloc(result, result_cap);
            }

            strcat(result, "$(");
            result_len += 2;
            ptr = start + 2;
        }

        start = strstr(ptr, "$(");
    }

    size_t rest_len = strlen(ptr);
    if (result_len + rest_len >= result_cap)
    {
        result_cap += rest_len + 1;
        result = realloc(result, result_cap);
    }
    strcat(result, ptr);

    return result;
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