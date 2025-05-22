#include <assert.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <fs.h>

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

#define test_fs_error(fmt, ...) \
	fprintf(stderr, "%s: "fmt"\n", __func__, ##__VA_ARGS__)

#define die(...)				\
do {							\
	test_fs_error(__VA_ARGS__);	\
	exit(1);					\
} while (0)

#define die_perror(msg)			\
do {							\
	perror(msg);				\
	exit(1);					\
} while (0)


struct thread_arg {
	int argc;
	char **argv;
};

void thread_fs_info(void *arg)
{
	struct thread_arg *t_arg = arg;
	char *diskname;

	if (t_arg->argc < 1)
		die("Usage: <diskname>");

	diskname = t_arg->argv[0];

	if (fs_mount(diskname))
		die("Cannot mount diskname");

	fs_info();

	if (fs_umount())
		die("Cannot unmount diskname");
}

size_t get_argv(char *argv)
{
	long int ret = strtol(argv, NULL, 0);
	if (ret == LONG_MIN || ret == LONG_MAX)
		die_perror("strtol");
	return (size_t)ret;
}

static struct {
	const char *name;
	void(*func)(void *);
} commands[] = {
	{ "info",	thread_fs_info },
};

void usage(char *program)
{
	size_t i;
	fprintf(stderr, "Usage: %s <command> [<arg>]\n", program);
	fprintf(stderr, "Possible commands are:\n");
	for (i = 0; i < ARRAY_SIZE(commands); i++)
		fprintf(stderr, "\t%s\n", commands[i].name);
	exit(1);
}

int main(int argc, char **argv)
{
	size_t i;
	char *program;
	char *cmd;
	struct thread_arg arg;

	program = argv[0];

	if (argc == 1)
		usage(program);

	/* Skip argv[0] */
	argc--;
	argv++;

	cmd = argv[0];
	arg.argc = --argc;
	arg.argv = &argv[1];

	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		if (!strcmp(cmd, commands[i].name)) {
			commands[i].func(&arg);
			break;
		}
	}
	if (i == ARRAY_SIZE(commands)) {
		test_fs_error("invalid command '%s'", cmd);
		usage(program);
	}

	return 0;
}
