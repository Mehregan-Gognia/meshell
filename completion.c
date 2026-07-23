#include "shell.h"

static char **matches = NULL;
static int matches_count = 0;
static int matches_cap = 0;
static int match_index = 0;

static void add_match(const char *str)
{
    for (int i = 0; i < matches_count; i++)
    {
        if (strcmp(matches[i], str) == 0)
            return; // Deduplicate matches
    }

    if (matches_count >= matches_cap)
    {
        matches_cap = matches_cap ? matches_cap * 2 : 16;
        matches = realloc(matches, matches_cap * sizeof(char *));
    }

    matches[matches_count++] = strdup(str);
}

// Generator function for command completion (builtins + $PATH executables)
static char *command_generator(const char *text, int state)
{
    if (state == 0)
    {
        if (matches != NULL)
        {
            for (int i = 0; i < matches_count; i++)
            {
                free(matches[i]);
            }

            free(matches);
            matches = NULL;
        }

        matches_count = 0;
        matches_cap = 0;
        match_index = 0;

        size_t len = strlen(text);

        // 1. Builtin commands
        static const char *builtins[] = {"cd", "exit", "export", "fg", "bg", "exec", NULL};

        for (int i = 0; builtins[i] != NULL; i++)
        {
            if (strncmp(builtins[i], text, len) == 0)
            {
                add_match(builtins[i]);
            }
        }

        // 2. Executables in $PATH
        char *path_env = getenv("PATH");
        if (path_env)
        {
            char *path_copy = strdup(path_env);
            char *saveptr = NULL;
            char *dir_path = strtok_r(path_copy, ":", &saveptr);
            while (dir_path != NULL)
            {
                DIR *dir = opendir(dir_path);
                if (dir != NULL)
                {
                    struct dirent *de;
                    while ((de = readdir(dir)) != NULL)
                    {
                        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
                            continue;

                        if (strncmp(de->d_name, text, len) == 0)
                        {
                            char full_path[4096];
                            snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, de->d_name);
                            if (access(full_path, X_OK) == 0)
                            {
                                struct stat st;
                                if (stat(full_path, &st) == 0 && !S_ISDIR(st.st_mode))
                                {
                                    add_match(de->d_name);
                                }
                            }
                        }
                    }

                    closedir(dir);
                }

                dir_path = strtok_r(NULL, ":", &saveptr);
            }

            free(path_copy);
        }
    }

    if (match_index < matches_count)
    {
        return strdup(matches[match_index++]);
    }

    return NULL;
}

// Custom completion function: completes commands for word 0, falls back to filenames otherwise
static char **shell_completion(const char *text, int start, int end)
{
    (void)end;
    rl_attempted_completion_over = 0;

    if (start == 0 && strchr(text, '/') == NULL)
    {
        return rl_completion_matches(text, command_generator);
    }

    return NULL;
}

void init_completion(void)
{
    rl_attempted_completion_function = shell_completion;
}