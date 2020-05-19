// No Copyright. Vladislav Aleinik 2020
//=====================================
// Logging Utilities
//=====================================
#ifndef NBD_SERVER_LOGGING_HPP_INCLUDED
#define NBD_SERVER_LOGGING_HPP_INCLUDED

// Feature test macros:
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdlib.h>
// Dprinf:
 #include <stdio.h>
// Time:
#include <sys/time.h>
#include <time.h>
// Strcmp:
#include <string.h>
// Open:
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
// Close:
#include <unistd.h>

//------------------------
// Log File Manipulations 
//------------------------

const char* LOG_FILE = "LOG.txt";

__attribute__((unused)) static int acquire_log_fd(const char* input_format)
{
#ifndef LOG_TO_STDOUT
	static int log_fd = -1;

	// Get current time:
	struct timeval cur_time;

	if (gettimeofday(&cur_time, NULL) == -1)
	{
		fprintf(stderr, "[ERROR] Unable to get time of day\n");
		exit(EXIT_FAILURE);
	}

	struct tm* broken_down_time = localtime(&cur_time.tv_sec);
	if (broken_down_time == NULL)
	{
		fprintf(stderr, "[ERROR] Unable to get broken-down time\n");
		exit(EXIT_FAILURE);
	}

	// Get a nice readable time string:
	char time_str_buf[128];
	if (strftime(time_str_buf, sizeof(time_str_buf), "%Y-%m-%d %H:%M:%S", broken_down_time) == 0)
	{
       fprintf(stderr, "[ERROR] Unable to get a nice readable time string\n");
       exit(EXIT_FAILURE);
   	}

	if (log_fd == -1)
	{
		log_fd = open(LOG_FILE, O_WRONLY|O_CREAT, 0600);
		if (log_fd == -1)
		{
			fprintf(stderr, "[ERROR %s:%06ld] Unable to open log file %s\n", time_str_buf, cur_time.tv_usec, LOG_FILE);
			exit(EXIT_FAILURE);
		}

		dprintf(log_fd, "[LOG %s:%06ld] Opened log file %s\n", time_str_buf, cur_time.tv_usec, LOG_FILE);
	}
	
	if (strcmp(input_format, "Closed log file") == 0 && log_fd != -1)
	{
		dprintf(log_fd, "[LOG %s:%06ld] Closed log file %s\n", time_str_buf, cur_time.tv_usec, LOG_FILE);
		close(log_fd);
		log_fd = -1;
	}

	return log_fd;
#else
	return 1;
#endif
}

// Ultra-super-duper hack to allow semicolon after macro:
// LOG_ERROR("BRUH"); <- like this
__attribute__((unused)) static void nop() {}

//----------------
// Logging Macros 
//----------------

#define LOG(format, ...)																			\
{																									\
	int log_fd = acquire_log_fd(format);															\
																									\
	struct timeval cur_time;																		\
																									\
	if (gettimeofday(&cur_time, NULL) == -1)														\
	{																								\
		fprintf(stderr, "[ERROR] Unable to get time of day\n");										\
		exit(EXIT_FAILURE);																			\
	}																								\
																									\
	struct tm* broken_down_time = localtime(&cur_time.tv_sec);										\
	if (broken_down_time == NULL)																	\
	{																								\
		fprintf(stderr, "[ERROR] Unable to get broken-down time\n");								\
		exit(EXIT_FAILURE);																			\
	}																								\
																									\
	char time_str_buf[128];																			\
	if (strftime(time_str_buf, sizeof(time_str_buf), "%Y-%m-%d %H:%M:%S", broken_down_time) == 0)	\
	{																								\
       fprintf(stderr, "[ERROR] Unable to get a nice readable time string\n");						\
       exit(EXIT_FAILURE);																			\
   	}																								\
																									\
	dprintf(log_fd, "[LOG %s:%06ld] ", time_str_buf, cur_time.tv_usec);								\
	dprintf(log_fd, format, ##__VA_ARGS__);															\
	dprintf(log_fd, "\n");																			\
} nop()

#define LOG_ERROR(format, ...)																		\
{																									\
	acquire_log_fd(format);																			\
																									\
	struct timeval cur_time;																		\
																									\
	if (gettimeofday(&cur_time, NULL) == -1)														\
	{																								\
		fprintf(stderr, "[ERROR] Unable to get time of day\n");										\
		exit(EXIT_FAILURE);																			\
	}																								\
																									\
	struct tm* broken_down_time = localtime(&cur_time.tv_sec);										\
	if (broken_down_time == NULL)																	\
	{																								\
		fprintf(stderr, "[ERROR] Unable to get broken-down time\n");								\
		exit(EXIT_FAILURE);																			\
	}																								\
																									\
	char time_str_buf[128];																			\
	if (strftime(time_str_buf, sizeof(time_str_buf), "%Y-%m-%d %H:%M:%S", broken_down_time) == 0)	\
	{																								\
       fprintf(stderr, "[ERROR] Unable to get a nice readable time string\n");						\
       exit(EXIT_FAILURE);																			\
   	}																								\
																									\
	fprintf(stderr, "[ERROR %s:%06ld]", time_str_buf, cur_time.tv_usec);							\
	fprintf(stderr, format, ##__VA_ARGS__);															\
	fprintf(stderr, "\n");																			\
} nop()

#define BUG_ON(condition, format, ...)																	\
{																										\
	if (condition)																						\
	{																									\
		acquire_log_fd(format);																			\
																										\
		struct timeval cur_time;																		\
																										\
		if (gettimeofday(&cur_time, NULL) == -1)														\
		{																								\
			fprintf(stderr, "[ERROR] Unable to get time of day\n");										\
			exit(EXIT_FAILURE);																			\
		}																								\
																										\
		struct tm* broken_down_time = localtime(&cur_time.tv_sec);										\
		if (broken_down_time == NULL)																	\
		{																								\
			fprintf(stderr, "[ERROR] Unable to get broken-down time\n");								\
			exit(EXIT_FAILURE);																			\
		}																								\
																										\
		char time_str_buf[128];																			\
		if (strftime(time_str_buf, sizeof(time_str_buf), "%Y-%m-%d %H:%M:%S", broken_down_time) == 0)	\
		{																								\
	       fprintf(stderr, "[ERROR] Unable to get a nice readable time string\n");						\
	       exit(EXIT_FAILURE);																			\
	   	}																								\
																										\
		fprintf(stderr, "[BUG %s:%06ld]", time_str_buf, cur_time.tv_usec);								\
		fprintf(stderr, format, ##__VA_ARGS__);															\
		fprintf(stderr, "\n");																			\
																										\
		exit(EXIT_FAILURE);																				\
	}																									\
} nop()

//-----------------------------
// Dynamically update log file
//-----------------------------

int set_log_file(const char* log_file)
{
	if (log_file == NULL)
	{
		time_t cur_time = time(NULL);
		fprintf(stderr, "[ERROR %s\r][set_log_file] Null log file name\n", ctime(&cur_time));
		return -1;
	}

	LOG_FILE = log_file;

	LOG(" Changed log file to %s", log_file);

	return 0;
}

#endif // NBD_SERVER_LOGGING_HPP_INCLUDED