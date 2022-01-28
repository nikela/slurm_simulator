#include "slurm/slurm.h"
#include "src/common/log.h"
#include "src/common/xstring.h"
#include "src/common/xmalloc.h"

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>


int sim_pthread_create (pthread_t *newthread,
		const pthread_attr_t *attr,
		void *(*start_routine) (void *),
		void *arg,
		const char *id,
		const char *func,
		const char *filename,
		const char *note)
{
	//slurmctld: debug:  id: '&thread_id_event_thread'
	//slurmctld: debug:  func: 'sim_events_thread'
	//slurmctld: debug:  id: '&backfill_thread'
	//slurmctld: debug:  func: 'backfill_agent

	debug("sim_pthread_create_all: id=%s func=%s note=%s file=%s",id,func,note,filename);

	// @TODO check that 'id' do not change and they are unique across slurm
	if (xstrcmp("&slurmctld_config.thread_id_rpc", id) == 0) {
		debug("sim_pthread_create: %s ... start.", id);
	} else if (xstrcmp("&slurmctld_config.thread_id_sig", id) == 0) {
		debug("sim_pthread_create: %s ... skip.", id);
		return 0;
	} else if (xstrcmp("&slurmctld_config.thread_id_save", id) == 0) {
		debug("sim_pthread_create: %s ... start.", id);
		//return 0;
	} else if (xstrcmp("&slurmctld_config.thread_id_power", id) == 0) {
		debug("sim_pthread_create: %s ... skip.", id);
		return 0;
	} else if ((xstrcmp("thread_id", id) == 0) && (xstrcmp("_init_power_save", func) == 0)) {
		debug("sim_pthread_create: %s %s ... skip.", id,func);
		//return 0;
	} else if (xstrcmp("&slurmctld_config.thread_id_purge_files", id) == 0) {
		debug("sim_pthread_create: %s ... skip.", id);
		return 0;
	} else if (xstrcmp("&thread_wdog", id) == 0) {
		debug("sim_pthread_create: %s ... skip.", id);
		//return 0;
	} else {
		debug("sim_pthread_create_passed: id=%s func=%s note=%s file=%s",id,func,note,filename);
	}
	//debug("id: '%s'", id);
	//debug("func: '%s'", func);

	int err = pthread_create(newthread, attr, start_routine, arg);

	if (xstrcmp("&backfill_thread", id) == 0) {
		debug("backfill_thread");
		//sim_plugin_sched_thread=*newthread;
		//sim_plugin_sched_thread_isset = 1;
	} else if (xstrcmp("&builtin_thread", id) == 0) {
		debug("builtin_thread");
		//sim_plugin_sched_thread = *newthread;
		//sim_plugin_sched_thread_isset = 1;
	}
	return err;
}
