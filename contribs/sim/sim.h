#ifndef _SIM_H
#define _SIM_H


extern int64_t simulator_start_time;
extern int64_t sim_constructor_start_time;

/*threads*/
extern pthread_t sim_main_thread;
extern int sim_plugin_sched_thread_isset;
extern pthread_t sim_plugin_sched_thread;

#endif
