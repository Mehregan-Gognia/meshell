#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

int main()
{
    char *buffer = NULL;
    size_t buffer_size = 0;
    ssize_t characters_read;

    while (1)
    {
        printf("$ ");
        fflush(stdout);

        characters_read = getline(&buffer, &buffer_size, stdin);

        if (characters_read == -1)
        {
            printf("\nExiting shell...\n");
        }

        buffer[strcspn(buffer, "\n")] = '\0';

        char *args[100];
        int i = 0;

        char *token = strtok(buffer, " ");
        while (token != NULL)
        {
            args[i++] = token;
            token = strtok(NULL, " ");
        }
        args[i] = NULL;

        pid_t pid = fork();

        if (pid < 0)
        {
            perror("fork failed");
        }
        else if (pid == 0)
        {
            // child process
            if (execvp(args[0], args) == -1)
            {
                perror("execvp failed");
            }
            exit(1);
        }
        else
        {
            // parent process
            int status;

            // wait for child process
            waitpid(pid, &status, 0);
        }
    }

    free(buffer);

    return 0;
}