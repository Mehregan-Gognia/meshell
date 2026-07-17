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
