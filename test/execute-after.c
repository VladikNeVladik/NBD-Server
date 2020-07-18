// No copyright. Vladislav Aleinik 2020
// stdlib:
#include <stdlib.h>
// fprintf():
#include <stdio.h>
// fork():
#include <sys/types.h>
#include <unistd.h>
// nanosleep():
#include <time.h>

int main(int argc, char* argv[])
{
	if (argc < 3)
	{
		fprintf(stderr, "[USAGE] execute-after <milliseconds-to-wait> <program-to-run>\n");
		return EXIT_FAILURE;
	}

	// Parse milliseconds to wait:
	char* endptr = argv[1];
	long milliseconds_to_wait = strtol(argv[1], &endptr, 10);
	if (*argv[1] == '\0' || *endptr != '\0')
	{
		fprintf(stderr, "[ERROR] Unable to parse milliseconds to wait\n");
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
		// Chill a bit:
		struct timespec sleep_request = {milliseconds_to_wait/1000, (milliseconds_to_wait % 1000) * 1000000};
		nanosleep(&sleep_request, NULL);

		// Run the program (in the background)
		if (execvp(argv[2], &argv[2]) == -1)
		{
			fprintf(stderr, "[ERROR] Unable to call execvp()\n");
			return EXIT_FAILURE;
		}
	}

	return EXIT_SUCCESS;
}