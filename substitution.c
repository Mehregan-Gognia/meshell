#include "shell.h"

// captures the stdout of a command by executing it in a child process and reading from a pipe
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
        // child
        // disable job control so background evaluation doesn't hijack foreground focus
        job_control_enabled = 0;

        // stdout to the write end
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

// parsing a string left to right, processing any $(...) blocks safely respecting quote states
char *expand_all_substitutions(char *src)
{
    if (src == NULL)
        return NULL;

    size_t result_cap = strlen(src) + 512;
    char *result = malloc(result_cap);
    result[0] = '\0';
    size_t result_len = 0;

    int in_single_quote = 0;
    int in_double_quote = 0;

    char *ptr = src;
    while (*ptr != '\0')
    {
        // Track quote states to isolate single quote literals
        if (*ptr == '"' && !in_single_quote)
        {
            in_double_quote = !in_double_quote;
            if (result_len + 1 >= result_cap)
            {
                result_cap += 512;
                result = realloc(result, result_cap);
            }
            result[result_len++] = *ptr;
            result[result_len] = '\0';
            ptr++;
        }
        else if (*ptr == '\'' && !in_double_quote)
        {
            in_single_quote = !in_single_quote;
            if (result_len + 1 >= result_cap)
            {
                result_cap += 512;
                result = realloc(result, result_cap);
            }
            result[result_len++] = *ptr;
            result[result_len] = '\0';
            ptr++;
        }
        // ONLY trigger command substitution if NOT wrapped in single quotes
        else if (strncmp(ptr, "$(", 2) == 0 && !in_single_quote)
        {
            char *start = ptr;
            char *sub_ptr = ptr + 2;
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

                *sub_ptr = ')';    // restore literal character
                ptr = sub_ptr + 1; // advance past the closing ')'
            }
            else
            {
                if (result_len + 2 >= result_cap)
                {
                    result_cap += 512;
                    result = realloc(result, result_cap);
                }
                strcat(result, "$(");
                result_len += 2;
                ptr += 2;
            }
        }
        else
        {
            if (result_len + 1 >= result_cap)
            {
                result_cap += 512;
                result = realloc(result, result_cap);
            }
            result[result_len++] = *ptr;
            result[result_len] = '\0';
            ptr++;
        }
    }

    return result;
}

// parses a string left-to-right, expanding environment variables ($VAR) and the exit status ($?)
char *expand_environment_variables(char *src, int last_exit_code)
{
    if (src == NULL)
    {
        return NULL;
    }

    size_t res_cap = strlen(src) + 512;
    char *result = malloc(res_cap);
    result[0] = '\0';
    size_t result_len = 0;

    int in_single_quote = 0;
    int in_double_quote = 0;

    char *p = src;
    while (*p != '\0')
    {
        // Track quote states to isolate single quote literals
        if (*p == '"' && !in_single_quote)
        {
            in_double_quote = !in_double_quote;
            if (result_len + 1 >= res_cap)
            {
                res_cap += 512;
                result = realloc(result, res_cap);
            }
            result[result_len++] = *p;
            result[result_len] = '\0';
            p++;
        }
        else if (*p == '\'' && !in_double_quote)
        {
            in_single_quote = !in_single_quote;
            if (result_len + 1 >= res_cap)
            {
                res_cap += 512;
                result = realloc(result, res_cap);
            }
            result[result_len++] = *p;
            result[result_len] = '\0';
            p++;
        }
        // ONLY expand variable values if NOT wrapped in single quotes
        else if (*p == '$' && !in_single_quote)
        {
            p++; // Move past '$'

            // Handle special exit code status case: $?
            if (*p == '?')
            {
                p++;
                char val_str[32];
                sprintf(val_str, "%d", last_exit_code);
                size_t val_len = strlen(val_str);

                if (result_len + val_len >= res_cap)
                {
                    res_cap += val_len + 512;
                    result = realloc(result, res_cap);
                }
                strcat(result, val_str);
                result_len += val_len;
                continue;
            }

            if (*p == '!')
            {
                p++;
                if (last_job_pid > 0)
                {
                    char val_str[32];
                    sprintf(val_str, "%d", last_job_pid);
                    size_t val_len = strlen(val_str);

                    if (result_len + val_len >= res_cap)
                    {
                        res_cap += val_len + 512;
                        result = realloc(result, res_cap);
                    }
                    strcat(result, val_str);
                    result_len += val_len;
                }
                // If no background job has run, POSIX states $! expands to nothing
                continue;
            }

            // Extract the variable name (alphanumeric characters and underscores)
            char var_name[256];
            int var_len = 0;
            while ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
                   (*p >= '0' && *p <= '9') || *p == '_')
            {
                if (var_len < 255)
                {
                    var_name[var_len++] = *p;
                }
                p++;
            }
            var_name[var_len] = '\0';

            if (var_len > 0)
            {
                char *val = get_shell_var(var_name);
                if (val == NULL)
                {
                    val = getenv(var_name);
                }

                if (val != NULL)
                {
                    size_t val_len = strlen(val);
                    if (result_len + val_len >= res_cap)
                    {
                        res_cap += val_len + 512;
                        result = realloc(result, res_cap);
                    }
                    strcat(result, val);
                    result_len += val_len;
                }
            }
            else
            {
                if (result_len + 1 >= res_cap)
                {
                    res_cap += 512;
                    result = realloc(result, res_cap);
                }
                result[result_len++] = '$';
                result[result_len] = '\0';
            }
        }
        else
        {
            if (result_len + 1 >= res_cap)
            {
                res_cap += 512;
                result = realloc(result, res_cap);
            }
            result[result_len++] = *p;
            result[result_len] = '\0';
            p++;
        }
    }

    return result;
}
// Processes a null-terminated array of arguments, expanding any tokens containing wildcards (* or ?)
char **apply_globbing(char **args)
{
    int max_args = 2048;
    char **new_args = malloc(sizeof(char *) * max_args);
    int new_argc = 0;

    for (int i = 0; args[i] != NULL; i++)
    {
        // Only run glob expansion if the token contains unquoted wildcards
        if (strchr(args[i], '*') != NULL || strchr(args[i], '?') != NULL)
        {
            glob_t glob_res;
            int ret = glob(args[i], GLOB_NOCHECK, NULL, &glob_res);
            if (ret == 0)
            {
                for (size_t j = 0; j < glob_res.gl_pathc; j++)
                {
                    if (new_argc < max_args - 1)
                    {
                        new_args[new_argc++] = strdup(glob_res.gl_pathv[j]);
                    }
                }
                globfree(&glob_res);
            }
            else
            {
                if (new_argc < max_args - 1)
                {
                    new_args[new_argc++] = strdup(args[i]);
                }
            }
        }
        else
        {
            if (new_argc < max_args - 1)
            {
                new_args[new_argc++] = strdup(args[i]);
            }
        }
    }
    new_args[new_argc] = NULL;

    // Restore any escaped wildcards back to their literal characters
    for (int k = 0; new_args[k] != NULL; k++)
    {
        for (int m = 0; new_args[k][m] != '\0'; m++)
        {
            if (new_args[k][m] == '\x01')
                new_args[k][m] = '*';
            else if (new_args[k][m] == '\x02')
                new_args[k][m] = '?';
        }
    }

    return new_args;
}
