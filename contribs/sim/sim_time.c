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

//should be in shared memory
extern int64_t *sim_timeval_shift;
extern double *sim_timeval_scale;

int64_t process_create_time_real = 0;
int64_t process_create_time_sim = 0;

#define real_gettimeofday gettimeofday

/* return real time in microseconds */
int64_t get_real_utime()
{
	struct timeval cur_real_time;
    real_gettimeofday(&cur_real_time, NULL);

	int64_t cur_real_utime = (int64_t) (cur_real_time.tv_sec) * (int64_t) 1000000 + (int64_t) (cur_real_time.tv_usec);
	return cur_real_utime;
}

/* return simulated time in microseconds */
int64_t get_sim_utime()
{
	return get_real_utime();
	/*int64_t cur_real_utime = get_real_utime();
	int64_t cur_sim_time = cur_real_utime + *sim_timeval_shift + (int64_t)((*sim_timeval_scale - 1.0)*cur_real_utime);
	return cur_sim_time;*/
}

void set_sim_time_and_scale(int64_t cur_sim_time, double scale)
{
	struct timeval cur_real_time;
	real_gettimeofday(&cur_real_time, NULL);

	int64_t cur_real_utime = (int64_t) (cur_real_time.tv_sec) * (int64_t) 1000000 + (int64_t) (cur_real_time.tv_usec);

	*sim_timeval_scale = scale;
	// essentially cur_sim_time - (*sim_timeval_scale)*cur_real_utime
	// reformatted to avoid overflow
	*sim_timeval_shift = (int64_t)((1.0-*sim_timeval_scale)*cur_sim_time) -
			(int64_t)(*sim_timeval_scale * (cur_real_utime - cur_sim_time));

	debug2("sim_timeval_shift %ld sim_timeval_scale %f\n\n", *sim_timeval_shift, *sim_timeval_scale);
}

void set_sim_time_scale(double scale)
{
	if (scale != *sim_timeval_scale) {
		set_sim_time_and_scale(get_sim_utime(), scale);
	}
}

void set_sim_time(int64_t cur_sim_time)
{
	set_sim_time_and_scale(cur_sim_time, *sim_timeval_scale);
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

/* initialize simulation time */
void init_sim_time(uint32_t start_time, double scale, int set_time, int set_time_to_real)
{
	int64_t cur_sim_time;
	int64_t cur_real_time;

	//determine_libc();
	//set_pointers_to_time_func();

	if (set_time_to_real > 0 || start_time==0) {
		cur_sim_time = get_real_utime();
	} else {
		cur_sim_time = (int64_t) start_time * (int64_t) 1000000;
	}

	if (set_time > 0) {
		set_sim_time_and_scale(cur_sim_time, scale);
	}

	cur_sim_time = get_sim_utime();
	cur_real_time = get_real_utime();

	process_create_time_real = get_process_create_time();
	process_create_time_sim = process_create_time_real + (cur_sim_time - cur_real_time);

	//info("sim: process create utime: %" PRId64 " process create utime: %" PRId64,
	//		process_create_time_real, process_create_time_sim);
	//info("sim: current real utime: %" PRId64 ", current sim utime: %" PRId64,
	//		cur_real_time, cur_sim_time);
}

void iso8601_from_utime(char **buf, uint64_t utime, bool msec)
{
	char p[64] = "";
	struct timeval tv;
	struct tm tm;

	tv.tv_sec = utime / 1000000;
	tv.tv_usec = utime % 1000000;

	if (!localtime_r(&tv.tv_sec, &tm))
		fprintf(stderr, "localtime_r() failed\n");

	if (strftime(p, sizeof(p), "%Y-%m-%dT%T", &tm) == 0)
		fprintf(stderr, "strftime() returned 0\n");

	if (msec)
		_xstrfmtcat(buf, "%s.%3.3d", p, (int)(tv.tv_usec / 1000));
	else
		_xstrfmtcat(buf, "%s", p);
}

