#ifndef _SIM_TIME_H
#define _SIM_TIME_H

#include <stdint.h>

/* process creation real time in microseconds */
extern int64_t process_create_time_real;
/* process creation simulated time  in microseconds */
extern int64_t process_create_time_sim;

/* return real time in microseconds */
extern int64_t get_real_utime();
/* return process create time in microseconds */
extern int64_t get_process_create_time();

extern void set_sim_time_and_scale(int64_t cur_sim_time, double scale);
extern void set_sim_time_scale(double scale);
extern void set_sim_time(int64_t cur_sim_time);

/* initialize simulation time */
void init_sim_time(uint32_t start_time, double scale, int set_time, int set_time_to_real);

#endif
