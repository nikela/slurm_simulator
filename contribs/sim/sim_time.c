#include "../../src/common/log.h"
#include "../../src/common/xmalloc.h"
#include "../../src/common/xstring.h"

#include <stdbool.h>
#include <stdlib.h>
#include <dlfcn.h>

#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <stdint.h>
#include <fcntl.h>
#include <stdio.h>

#include <string.h>

#include <inttypes.h>

#include "sim_time.h"


/* return real time in microseconds */
int64_t get_real_utime()
{
	struct timeval cur_real_time;
    //real_gettimeofday(&cur_real_time, NULL);
	gettimeofday(&cur_real_time, NULL);

	int64_t cur_real_utime = (int64_t) (cur_real_time.tv_sec) * (int64_t) 1000000 + (int64_t) (cur_real_time.tv_usec);
	return cur_real_utime;
}

/* find index of n-th space */
int find_nth_space(char *search_buffer, int space_ordinality) {
	int jndex;
	int space_count;

	space_count = 0;

	for (jndex = 0; search_buffer[jndex]; jndex++) {
		if (search_buffer[jndex] == ' ') {
			space_count++;

			if (space_count >= space_ordinality) {
				return jndex;
			}
		}
	}

	fprintf(stderr, "looking for too many spaces\n");
	exit(1);
}


/* return process create time in microseconds */
int64_t get_process_create_time() {
	int field_begin;
	int stat_fd;

	const int stat_buf_size = 8192;
	char *stat_buf = xcalloc(stat_buf_size,1);

	long jiffies_per_second;

	int64_t boot_time_since_epoch;
	int64_t process_start_time_since_boot;

	int64_t process_start_time_since_epoch;

	ssize_t read_result;

	jiffies_per_second = sysconf(_SC_CLK_TCK);


	stat_fd = open("/proc/self/stat", O_RDONLY);

	if (stat_fd < 0) {
		fprintf(stderr, "open() fail\n");
		exit(1);
	}

	read_result = read(stat_fd, stat_buf, stat_buf_size);

	if (read_result < 0) {
		fprintf(stderr, "read() fail\n");
		exit(1);
	}

	if (read_result >= stat_buf_size) {
		fprintf(stderr, "stat_buf is too small\n");
		exit(1);
	}

	field_begin = find_nth_space(stat_buf, 21) + 1;

	stat_buf[find_nth_space(stat_buf, 22)] = 0;

	sscanf(stat_buf + field_begin, "%" PRId64, &process_start_time_since_boot);

	close(stat_fd);

	stat_fd = open("/proc/stat", O_RDONLY);

	if (stat_fd < 0) {
		fprintf(stderr, "open() fail\n");

		exit(1);
	}

	read_result = read(stat_fd, stat_buf, stat_buf_size);

	if (read_result < 0) {
		fprintf(stderr, "read() fail\n");

		exit(1);
	}

	if (read_result >= stat_buf_size) {
		fprintf(stderr, "stat_buf is too small\n");

		exit(1);
	}

	close(stat_fd);

	field_begin = strstr(stat_buf, "btime ") - stat_buf + 6;
	sscanf(stat_buf + field_begin, "%" PRId64, &boot_time_since_epoch);

	if(jiffies_per_second<=10000) {
		process_start_time_since_epoch = boot_time_since_epoch * 1000000
					+ (process_start_time_since_boot * 1000000) / jiffies_per_second;
	} else {
		double dtmp1=((double)process_start_time_since_boot/(double)jiffies_per_second)*1.0e6;
		process_start_time_since_epoch = boot_time_since_epoch * 1000000 + (int64_t)dtmp1;
	}

	xfree(stat_buf);
	return process_start_time_since_epoch;
}

