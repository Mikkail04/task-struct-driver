#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <string.h>

#include "scull.h"

#define CDEV_NAME "/dev/scull"

/* Quantum command line option */
static int g_quantum;

static void usage(const char *cmd)
{
	printf("Usage: %s <command>\n"
	       "Commands:\n"
	       "  i          Print information\n"
               "  p          Print processes\n"
	       "  R          Reset quantum\n"
	       "  S <int>    Set quantum\n"
	       "  T <int>    Tell quantum\n"
	       "  G          Get quantum\n"
	       "  Q          Query quantum\n"
	       "  X <int>    Exchange quantum\n"
	       "  H <int>    Shift quantum\n"
	       "  h          Print this message\n",
	       cmd);
}

typedef int cmd_t;

static cmd_t parse_arguments(int argc, const char **argv)
{
	cmd_t cmd;

	if (argc < 2) {
		fprintf(stderr, "%s: Invalid number of arguments\n", argv[0]);
		cmd = -1;
		goto ret;
	}

	/* Parse command and optional int argument */
	cmd = argv[1][0];
	switch (cmd) {
	case 'S':
	case 'T':
	case 'H':
	case 'X':
		if (argc < 3) {
			fprintf(stderr, "%s: Missing quantum\n", argv[0]);
			cmd = -1;
			break;
		}
		g_quantum = atoi(argv[2]);
		break;
	case 'R':
	case 'G':
	case 'Q':
	case 'h':
	case 'i':
	case 'p':
		break;
	default:
		fprintf(stderr, "%s: Invalid command\n", argv[0]);
		cmd = -1;
	}

ret:
	if (cmd < 0 || cmd == 'h') {
		usage(argv[0]);
		exit((cmd == 'h')? EXIT_SUCCESS : EXIT_FAILURE);
	}
	return cmd;
}

static void do_process(int fd)
{
    struct task_info info;
 
   // Use SCULL_IOCIQUANTUM ioctl twice
    ioctl(fd, SCULL_IOCIQUANTUM, &info);
    printf("state %u, cpu %u, prio %d, pid %d, tgid %d, nv %lu, niv %lu\n",
           info.__state, info.cpu, info.prio, info.pid, info.tgid, info.nvcsw, info.nivcsw);
    ioctl(fd, SCULL_IOCIQUANTUM, &info);
    printf("state %u, cpu %u, prio %d, pid %d, tgid %d, nv %lu, niv %lu\n",
           info.__state, info.cpu, info.prio, info.pid, info.tgid, info.nvcsw, info.nivcsw);
}

static int do_op(int fd, cmd_t cmd)
{
	int ret, q;
	struct task_info info;
	pid_t pid;

	switch (cmd) {
	case 'p':
	// Create four child processes
        for (int i = 0; i < 4; ++i) {
        	pid = fork();

		if (pid == 0) { 
                	do_process(fd);
                	exit(EXIT_SUCCESS);
		}
		if (pid < 0) {
                	perror("fork");
                	exit(EXIT_FAILURE);
		}
        }

        // Wait for all child processes to terminate
        for (int i = 0; i < 4; ++i) {
        	wait(NULL);
        }
        break;
	case 'i':
		ret = ioctl(fd, SCULL_IOCIQUANTUM, &info);
        	if (ret == 0)
            		printf("state %u, cpu %u, prio %d, pid %d, tgid %d, nv %lu, niv %lu\n",
                   	info.__state, info.cpu, info.prio, info.pid, info.tgid, info.nvcsw, info.nivcsw);
        break;
	case 'R':
		ret = ioctl(fd, SCULL_IOCRESET);
		if (ret == 0)
			printf("Quantum reset\n");
		break;
	case 'Q':
		q = ioctl(fd, SCULL_IOCQQUANTUM);
		printf("Quantum: %d\n", q);
		ret = 0;
		break;
	case 'G':
		ret = ioctl(fd, SCULL_IOCGQUANTUM, &q);
		if (ret == 0)
			printf("Quantum: %d\n", q);
		break;
	case 'T':
		ret = ioctl(fd, SCULL_IOCTQUANTUM, g_quantum);
		if (ret == 0)
			printf("Quantum set\n");
		break;
	case 'S':
		q = g_quantum;
		ret = ioctl(fd, SCULL_IOCSQUANTUM, &q);
		if (ret == 0)
			printf("Quantum set\n");
		break;
	case 'X':
		q = g_quantum;
		ret = ioctl(fd, SCULL_IOCXQUANTUM, &q);
		if (ret == 0)
			printf("Quantum exchanged, old quantum: %d\n", q);
		break;
	case 'H':
		q = ioctl(fd, SCULL_IOCHQUANTUM, g_quantum);
		printf("Quantum shifted, old quantum: %d\n", q);
		ret = 0;
		break;
	default:
		/* Should never occur */
		abort();
		ret = -1; /* Keep the compiler happy */
	}

	if (ret != 0)
		perror("ioctl");
	return ret;
}

int main(int argc, const char **argv)
{
	int fd, ret;
	cmd_t cmd;

	cmd = parse_arguments(argc, argv);

	fd = open(CDEV_NAME, O_RDONLY);
	if (fd < 0) {
		perror("cdev open");
		return EXIT_FAILURE;
	}

	printf("Device (%s) opened\n", CDEV_NAME);

	ret = do_op(fd, cmd);

	if (close(fd) != 0) {
		perror("cdev close");
		return EXIT_FAILURE;
	}

	printf("Device (%s) closed\n", CDEV_NAME);

	return (ret != 0)? EXIT_FAILURE : EXIT_SUCCESS;
}
