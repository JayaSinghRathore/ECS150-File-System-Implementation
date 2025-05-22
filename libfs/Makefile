# libfs/Makefile

CC := gcc
CFLAGS := -Wall -Wextra -Werror -I.
AR := ar
ARFLAGS := rcs

LIB := libfs.a
OBJS := fs.o disk.o

all: $(LIB)

$(LIB): $(OBJS)
	$(AR) $(ARFLAGS) $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(LIB)
