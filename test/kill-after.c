// No copyright. Vladislav Aleinik 2020
// stdlib:
#include <stdlib.h>
// fprintf():
#include <stdio.h>
// fork():
#include <sys/types.h>
#include <unistd.h>
// kill():
#include <signal.h>
// nanosleep():
#include <time.h>
// errno:
#include <errno.h>

int main(int argc, char* argv[])
{
	if (argc < 3)
	{
		fprintf(stderr, "[USAGE] kill-after <milliseconds-to-live> <program-to-run>\n");
		return EXIT_FAILURE;
	}

	// Parse milliseconds to live:
	char* endptr = argv[1];
	long milliseconds_to_live = strtol(argv[1], &endptr, 10);
	if (*argv[1] == '\0' || *endptr != '\0')
	{
		fprintf(stderr, "[ERROR] Unable to parse milliseconds to live\n");
		return EXIT_FAILURE;
	}

	// Fork:
	int forked_pid = fork();
	if (forked_pid == -1)
	{
		fprintf(stderr, "[ERROR] Unable to fork()\n");
		return EXIT_FAILURE;
	}

	if (forked_pid == 0)
	{
		if (execvp(argv[2], &argv[2]) == -1)
		{
			fprintf(stderr, "[ERROR] Unable to call execvp()\n");
			return EXIT_FAILURE;
		}
	}
	else
	{
		int child_pid = forked_pid;

		// Let the child live for some time
		struct timespec sleep_request = {milliseconds_to_live/1000, (milliseconds_to_live % 1000) * 1000000};
		nanosleep(&sleep_request, NULL);

		if (kill(child_pid, SIGKILL) == -1)
		{
			if (errno == ESRCH)
			{
				printf("Program finished under %ldmsec\n", milliseconds_to_live);
				return EXIT_SUCCESS;
			}

			fprintf(stderr, "[ERROR] Unable to kill() the child\n");
			return EXIT_FAILURE;
		}
	}

	printf("Program is successfully terminated\n");
	return EXIT_SUCCESS;
}