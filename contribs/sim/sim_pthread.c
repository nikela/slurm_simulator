#include "slurm/slurm.h"
#include "src/common/log.h"
#include "src/common/xstring.h"
#include "src/common/xmalloc.h"

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>

#include "sim.h"
#include "sim_time.h"

extern char *__progname;


pthread_t sim_main_thread=0;
pthread_t sim_sched_thread=0;
pthread_t sim_plugin_backfill_thread=0;
pthread_t sim_thread_priority_multifactor=0;
pthread_t sim_agent_init = 0;

int64_t sim_main_thread_sleep_till = 0;
int64_t sim_sched_thread_cond_wait_till = 0;
int64_t sim_plugin_backfill_thread_sleep_till = 0;
// set some time to INT64_MAX so that they don't interfere with sleep calculation if not initialized
int64_t sim_thread_priority_multifactor_sleep_till = INT64_MAX;
int64_t sim_agent_init_sleep_till = INT64_MAX;

int sim_pthread_create (pthread_t *newthread,
		const pthread_attr_t *attr,
		void *(*start_routine) (void *),
		void *arg,
		const char *id,
		const char *func,
		const char *sarg,
		const char *funccall,
		const char *filename,
		const char *note,
		const int line)
{
	//slurmctld: debug:  id: '&thread_id_event_thread'
	//slurmctld: debug:  func: 'sim_events_thread'
	//slurmctld: debug:  id: '&backfill_thread'
	//slurmctld: debug:  func: 'backfill_agent

	// @TODO check that 'id' do not change and they are unique across slurm
	if(xstrcmp("slurmctld", __progname) == 0) {
		if (xstrcmp("&slurmctld_config.thread_id_rpc", id) == 0) {
			debug("sim_pthread_create: %s ... start.", id);
		} else if (xstrcmp("&slurmctld_config.thread_id_sig", id) == 0) {
			debug("sim_pthread_create: %s ... skip.", id);
			return 0;
		} else if (xstrcmp("&slurmctld_config.thread_id_save", id) == 0) {
			debug("sim_pthread_create: %s ... start.", id);
			return 0;
		} else if (xstrcmp("&slurmctld_config.thread_id_power", id) == 0) {
			debug("sim_pthread_create: %s ... skip.", id);
			return 0;
		} else if ((xstrcmp("thread_id", id) == 0) && (xstrcmp("_init_power_save", func) == 0)) {
			debug("sim_pthread_create: %s %s ... skip.", id,func);
			return 0;
		} else if (xstrcmp("&slurmctld_config.thread_id_purge_files", id) == 0) {
			debug("sim_pthread_create: %s ... skip.", id);
			return 0;
		} else if (xstrcmp("&thread_id_sched", id) == 0 && xstrcmp("main_sched_init", funccall) == 0) {
			debug("sim_pthread_create: %s ... skip.", id);
			return 0;
		} else if (xstrcmp("&thread_wdog", id) == 0) {
			debug("sim_pthread_create: %s ... skip.", id);
			//return 0;
		}  else if (xstrcmp("_service_connection", func) == 0) {
			debug("sim_pthread_create: %s ... .", func);
			//return 0;
		} else if (endswith(filename, "fed_mgr.c") == 0) {
			debug("sim_pthread_create: %s ... .", "fed_mgr.c");
			//return 0;
		} else {
			debug("sim_pthread_create_passed: id=%s func=%s note=%s file=%s line=%d",id,func,note,filename,line);
		}
	}
	//debug("id: '%s'", id);
	//debug("func: '%s'", func);


	int err = pthread_create(newthread, attr, start_routine, arg);
	debug2("sim_pthread_create_all: id=%s func=%s arg=%s funccall=%s note=%s file=%s thread=%lu threadcall=%lu",
			id,func,sarg,funccall,note,filename, *newthread, pthread_self());


	if (xstrcmp("&backfill_thread", id) == 0) {
		debug("thread up: backfill_thread");
		sim_plugin_backfill_thread=*newthread;
	} else if (xstrcmp("&builtin_thread", id) == 0) {
		debug("thread up: builtin_thread");
		sim_plugin_backfill_thread = *newthread;
	} else if (xstrcmp("&thread_id_sched", id) == 0) {
		debug("thread up: thread_id_sched_thread");
		sim_sched_thread = *newthread;
	} else if (xstrcmp("&decay_handler_thread ", id) == 0) {
		debug("thread up: decay_handler_thread ");
		sim_thread_priority_multifactor = *newthread;
		sim_thread_priority_multifactor_sleep_till = 0;
	} else if (xstrcmp("agent_init", funccall) == 0 && xstrcmp("_agent_init", func) == 0) {
		debug("thread up: agent_init ");
		sim_agent_init = *newthread;
		sim_agent_init_sleep_till = 0;
	}
	return err;
}


int sim_sched_requests=0;
int sim_pending_cond=0;

int slurm_cond_signal0 (pthread_cond_t * cond,
		const char *scond,
		const char *filename,
		const int line,
		const char *func)
{
	//debug3("slurm_cond_signal0 cond=%s func=%s file=%s thread=%lu",scond,func,filename, pthread_self());
	if (xstrcmp("schedule", func) == 0 && xstrcmp("&sched_cond", scond) == 0) {
		sim_sched_requests++;
	}

	int err = pthread_cond_signal(cond);
	if (err) {
		errno = err;
		error("%s:%d %s: pthread_cond_signal(): %m",
			__FILE__, __LINE__, __func__);
	}
	return err;
}


int slurm_cond_broadcast0 (pthread_cond_t * cond,
		const char *scond,
		const char *filename,
		const int line,
		const char *func)
{
	if (xstrcmp("&pending_cond", scond) == 0 && xstrcmp("agent_trigger", func) == 0) {
		sim_pending_cond++;
		return 0;
	}
	if (xstrcmp("&state_save_cond", scond) == 0) {
		//sim_pending_cond++;
		return 0;
	}
	if (xstrcmp("&slurmctld_config.thread_count_cond", scond) == 0) {
		return 0;
	}

	//debug3("slurm_cond_broadcast0 cond=%s func=%s file=%s thread=%lu",scond,func,filename, pthread_self());

	if (xstrcmp("&sched_cond", scond) == 0 && xstrcmp("schedule", func) == 0) {
		sim_sched_requests++;
		sim_sched_thread_cond_wait_till=0;//i.e. sched should start any time now
	}


	int err = pthread_cond_broadcast(cond);
	if (err) {
		errno = err;
		error("%s:%d %s: pthread_cond_broadcast(): %m",
			__FILE__, __LINE__, __func__);
	}
	return err;
}


void slurm_cond_timedwait1(pthread_cond_t *cond, pthread_mutex_t *mutex,
		const struct timespec *abstime,
		const char *scond,
		const char *filename,
		const int line,
		const char *func)
{
	int64_t abstime_sim = abstime->tv_sec * 1000000 + (abstime->tv_nsec/1000);
	int64_t real_utime = get_real_utime();
	int64_t sim_utime = get_sim_utime();
	int64_t abstime_real = abstime_sim + (real_utime-sim_utime);
	struct timespec abstime_real_ts;

	int err;


	abstime_real_ts.tv_sec = abstime_real/1000000;
	abstime_real_ts.tv_nsec = (abstime_real%1000000)*1000;

	err = pthread_cond_timedwait(cond, mutex, &abstime_real_ts);
	if (err && (err != ETIMEDOUT)) {
		errno = err;
		error("%s:%d %s: pthread_cond_timedwait(): %m",
				filename, line, func);
	}
}


int slurm_cond_wait0 (pthread_cond_t * cond, pthread_mutex_t * mutex,
		const char *scond,
		const char *filename,
		const int line,
		const char *func)
{
	int err;
	int sim_sched_requests_old;
	//debug3("slurm_cond_wait0 cond=%s func=%s file=%s thread=%lu",scond,func,filename, pthread_self());
	int64_t sim_utime = get_sim_utime();
	if( pthread_self()==sim_sched_thread ) {
		slurm_mutex_unlock(mutex);
        sim_sched_requests_old=sim_sched_requests;
		sim_sched_thread_cond_wait_till=sim_utime + 120000000;
		while(sim_sched_requests==sim_sched_requests_old){
			// keep it here or some weird optimization will happens
			get_sim_utime();
		}
		sim_sched_requests = 0;
		sim_sched_thread_cond_wait_till=0;
		slurm_mutex_lock(mutex);
		return 0;
	} else {
		do {
			err = pthread_cond_wait(cond, mutex);
			if (err) {
				errno = err;
				error("%s:%d %s: pthread_cond_wait(): %m",
					__FILE__, __LINE__, __func__);
			}
		} while (0);
		return err;
	}
}

void slurm_cond_timedwait0(pthread_cond_t *cond,
		pthread_mutex_t *mutex, const struct timespec *abstime,
		const char *scond,
		const char *filename,
		const int line,
		const char *func)
{
	int nanosecondswait=1000;
	int64_t abstime_sim = abstime->tv_sec * 1000000 + (abstime->tv_nsec/1000);
	int64_t real_utime = get_real_utime();
	int64_t sim_utime = get_sim_utime();
	int64_t abstime_real = abstime_sim + (real_utime-sim_utime);
	int64_t next_real_time;
	int64_t wait = abstime_sim - sim_utime;
	int64_t shortwait = wait > 0 && wait < 2000000;
	struct timespec ts;
	int err;
	struct timespec abstime_real_ts;
	int sim_cond_count_old;
	//debug3("slurm_cond_timedwait0 cond=%s func=%s file=%s thread=%lu",scond,func,filename, pthread_self());

	abstime_real_ts.tv_sec = abstime_real/1000000;
	abstime_real_ts.tv_nsec = (abstime_real%1000000)*1000;

	// @TODO check that that is the case in newer versions
	// back filler don't have case of cond triggering
	// yes it does
	if( pthread_self()==sim_sched_thread ) {
		slurm_mutex_unlock(mutex);
		sim_sched_thread_cond_wait_till = abstime_sim;
		sim_cond_count_old=sim_sched_requests;
		while(sim_utime < abstime_sim && sim_sched_requests==sim_cond_count_old){
			sim_utime = get_sim_utime();
		}
		sim_sched_requests = 0;
		sim_sched_thread_cond_wait_till = 0;
		slurm_mutex_lock(mutex);
		return;
	}
	if( pthread_self()==sim_plugin_backfill_thread ) {
		// @TODO check that that is the case in newer versions
		// back filler don't have case of cond triggering
		slurm_mutex_unlock(mutex);
		sim_plugin_backfill_thread_sleep_till = abstime_sim;
		if(!shortwait) {
			// let it work real time for a second before backfill attempt
			sim_plugin_backfill_thread_sleep_till = abstime_sim;//-1000000;
		}
		while(sim_utime < abstime_sim){
			sim_utime = get_sim_utime();
		}
		sim_plugin_backfill_thread_sleep_till = 0;
		slurm_mutex_lock(mutex);
		return;
	}
	if( pthread_self()==sim_thread_priority_multifactor ) {
		slurm_mutex_unlock(mutex);
		sim_thread_priority_multifactor_sleep_till = abstime_sim;
		while(sim_utime < abstime_sim){
			sim_utime = get_sim_utime();
		}
		sim_thread_priority_multifactor_sleep_till = 0;
		slurm_mutex_lock(mutex);
		return;
	}
	if( pthread_self()==sim_agent_init ) {
		slurm_mutex_unlock(mutex);
		sim_agent_init_sleep_till = abstime_sim;
		sim_cond_count_old=sim_agent_init;
		while(sim_utime < abstime_sim && sim_pending_cond==sim_cond_count_old){
			sim_utime = get_sim_utime();
		}
		sim_pending_cond = 0;
		sim_agent_init_sleep_till = 0;
		slurm_mutex_lock(mutex);
		return;
	}
	do {
		clock_gettime(CLOCK_REALTIME, &ts);

		ts.tv_nsec = ts.tv_nsec + nanosecondswait;

		if(ts.tv_nsec >=  1000000000) {
			ts.tv_sec += ts.tv_nsec / 1000000000;
			ts.tv_nsec = ts.tv_nsec % 1000000000;
		}

		next_real_time = ts.tv_sec * 1000000 + ts.tv_nsec / 1000;

		if(next_real_time < abstime_real) {
			next_real_time = abstime_real;
		}
		err = pthread_cond_timedwait(cond, mutex, &abstime_real_ts);
		if (err && (err != ETIMEDOUT)) {
			errno = err;
			error("%s:%d %s: pthread_cond_timedwait(): %m",
					filename, line, func);
			break;
		}
		if (err==0) {
			// i.e. got signal
			break;
		}

	} while (get_sim_utime() < abstime_sim);
}
