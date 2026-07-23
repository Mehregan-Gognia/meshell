CC      = gcc
CFLAGS  = -Wall -Wextra -g -O2
TARGET  = meshell
SRCS    = main.c parser.c executor.c substitution.c redirection.c completion.c
OBJS    = $(SRCS:.c=.o)
DEPS    = shell.h

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) -lreadline

%.o: %.c $(DEPS)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: clean
