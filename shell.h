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

extern int job_control_enabled; // track interactive context
extern pid_t last_job_pid; // track the last job's PID for job control
extern int next_job_bg;

int check_and_handle_redirections(char **args);
int execute_single_command(char **args);
int execute_pipeline(char *line);
char *capture_command_output(char *cmd);
char *expand_all_substitutions(char *src);
int execute_command_line(char *line);

#endif