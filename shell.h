#ifndef SHELL_H
#define SHELL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <stdbool.h>
#include <glob.h>
#include <pwd.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <dirent.h>
#include <sys/stat.h>

#define MAX_ARGS 1024
#define MAX_CMDS 1024
#define MAX_LOCAL_VARS 512

typedef struct
{
    char *name;
    char *value;
    int is_exported;
} local_var;

extern int job_control_enabled;
extern pid_t last_job_pid;
extern int next_job_bg;
extern int last_exit_status;

int check_and_handle_redirections(char **args);
int execute_single_command(char **args);
int execute_pipeline(char *line);
char *capture_command_output(char *cmd);
char *expand_all_substitutions(char *src);
int execute_command_line(char *line);
char *expand_environment_variables(char *src, int last_exit_code);
void set_shell_var(const char *name, const char *value);
char *get_shell_var(const char *name);
char **apply_globbing(char **args);
void init_completion(void);

#endif