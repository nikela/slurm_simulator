/*
 * this source wrap slurmctld/controller.c during slurmctld building
 * contains main simulated event loop
 */

#define main slurmctld_main
#include "../../src/slurmctld/controller.c"
#undef main

#include "../../contribs/sim/sim_time.h"
#include "../../contribs/sim/sim_conf.h"
#include "../../contribs/sim/sim_events.h"
#include "../../contribs/sim/sim_jobs.h"
#include "../../contribs/sim/sim.h"

#include <inttypes.h>
#include <signal.h>

pthread_t thread_id_event_thread;

extern void submit_job(sim_event_submit_batch_job_t* event_submit_batch_job);
extern int sim_init_slurmd(int argc, char **argv);

/*
 * read and remove simulation related arguments
 */
static void sim_slurmctld_parse_commandline(int *new_argc, char ***new_argv, int argc, char **argv)
{
	int i;
	int m_argc=0;
	char **m_argv = xcalloc(argc,sizeof(char*));

	for(i=0; i<argc; ++i){
		if(xstrcmp(argv[i],"-e")==0) {
			if(argc-1<=i) {
				error("Events file is not specified in command line!");
				exit(1);
			}
			xfree(slurm_sim_conf->events_file);
			slurm_sim_conf->events_file = xstrdup(argv[i+1]);
			info("will read events file from %s", slurm_sim_conf->events_file);
			++i;
		} else if(xstrcmp(argv[i],"-dtstart")==0) {
			if(argc-1<=i) {
				error("dtstart is not specified in command line!");
				exit(1);
			}
			slurm_sim_conf->microseconds_before_first_job=(int64_t)(atof(argv[i+1])*1000000);
			info("microseconds_before_first_job (dtstart) reset to %" PRId64 " usec", slurm_sim_conf->microseconds_before_first_job);
			++i;
		} else {
			m_argv[m_argc] = xstrdup(argv[i]);
			m_argc += 1;
		}
	}
	*new_argc=m_argc;
	*new_argv=m_argv;
}


void sim_complete_job(uint32_t job_id)
{
	//char *hostname;
	job_record_t *job_ptr = find_job_record(job_id);
	if(job_ptr==NULL){
		error("Can not find record for %d job!", job_id);
		sim_remove_active_sim_job(job_id);
		return;
	}
	debug2("Processing RPC: REQUEST_COMPLETE_BATCH_SCRIPT from "
		"uid=%u JobId=%u",
		job_ptr->user_id, job_id);

	if(IS_JOB_COMPLETING(job_ptr)){
		job_epilog_complete(job_ptr->job_id, "localhost", SLURM_SUCCESS);
		sim_remove_active_sim_job(job_id);
		return;
	}
	if(!IS_JOB_RUNNING(job_ptr)){
		error("Can not stop %d job, it is not running (%s (%d))!",
				job_id, job_state_string(job_ptr->job_state), job_ptr->job_state);
		sim_remove_active_sim_job(job_id);
		return;
	}
	//hostname = hostlist_shift(job_ptr->nodes);
	// REQUEST_COMPLETE_BATCH_SCRIPT
	/* Locks: Write job, write node, read federation */
	slurmctld_lock_t job_write_lock1 =
		{ .job  = WRITE_LOCK,
		  .node = WRITE_LOCK,
		  .fed  = READ_LOCK };

	lock_slurmctld(job_write_lock1);
	job_complete(job_ptr->job_id, job_ptr->user_id, false, false, SLURM_SUCCESS);
	unlock_slurmctld(job_write_lock1);

	sim_insert_event_epilog_complete(job_id);
}

void sim_epilog_complete(uint32_t job_id)
{
	//char *hostname;
	job_record_t *job_ptr = find_job_record(job_id);
	if(job_ptr==NULL){
		error("Can not find record for %d job!", job_id);
		sim_remove_active_sim_job(job_id);
		return;
	}

	if(IS_JOB_COMPLETING(job_ptr)){
		job_epilog_complete(job_ptr->job_id, "localhost", SLURM_SUCCESS);
		sim_remove_active_sim_job(job_id);
		return;
	}
	if(!IS_JOB_RUNNING(job_ptr)){
		error("Can not stop %d job, it is not running (%s (%d))!",
				job_id, job_state_string(job_ptr->job_state), job_ptr->job_state);
		sim_remove_active_sim_job(job_id);
		return;
	}

	// MESSAGE_EPILOG_COMPLETE
	slurmctld_lock_t job_write_lock2 = {
			READ_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK, NO_LOCK };
	lock_slurmctld(job_write_lock2);
	job_epilog_complete(job_ptr->job_id, "localhost", SLURM_SUCCESS);
	unlock_slurmctld(job_write_lock2);

	//free(hostname);
	sim_remove_active_sim_job(job_id);
}

extern int sim_registration_engine();


void *sim_events_thread(void *no_data)
{
	//time_t start_time;
	//int jobs_submit_count=0;
	static sim_event_t * event = NULL;
	static time_t all_done=0;
	char *stmp1 = xcalloc(128, sizeof(char));
	char *stmp2 = xcalloc(128, sizeof(char));

	int64_t now;
	int64_t cur_real_utime, cur_sim_utime;
	//int64_t slurmctld_diag_stats_lastcheck;

	/* time reference */
	sleep(1);

	info("sim: process create real utime: %" PRId64 ", process create sim utime: %" PRId64,
			process_create_time_real, process_create_time_sim);
	iso8601_from_utime(&stmp1, process_create_time_real, true);
	iso8601_from_utime(&stmp2, process_create_time_sim, true);
	info("sim: process create real time: %s, process create sim time: %s",
			stmp1, stmp2);

	cur_real_utime = get_real_utime();
	cur_sim_utime = get_sim_utime();
	info("sim: current real utime: %" PRId64 ", current sim utime: %" PRId64,
			cur_real_utime, cur_sim_utime);
	stmp1[0]=0;stmp2[0]=0;
	iso8601_from_utime(&stmp1, cur_real_utime, true);
	iso8601_from_utime(&stmp2, cur_sim_utime, true);
	info("sim: current real utime: %s, current sim utime: %s",
			stmp1, stmp2);

	/*init vars*/
	while(1) {

		now = get_sim_utime();
		//start_time = now;

		/* SIM Start */
		if(sim_next_event->when - now < 0) {
			while(sim_next_event->when - now < 0) {
				event = sim_next_event;
				pthread_mutex_lock(&events_mutex);
				sim_next_event = sim_next_event->next;
				pthread_mutex_unlock(&events_mutex);

				sim_print_event(event);

				switch(event->type) {
				case SIM_NODE_REGISTRATION:
					//sim_registration_engine();
					break;
				case SIM_SUBMIT_BATCH_JOB:
					submit_job((sim_event_submit_batch_job_t*)event->payload);
					break;
				case SIM_COMPLETE_BATCH_SCRIPT:
					//sim_complete_job(((sim_job_t*)event->payload)->job_id);
					break;
				case SIM_EPILOG_COMPLETE:
					//sim_epilog_complete(((sim_job_t*)event->payload)->job_id);
				default:
					break;
				}
			}
			//
			//jobs_submit_count++;
		}
		/*check can we skip some time*/
		/*int64_t skipping_to_utime = INT64_MAX;
		int64_t skip_usec;
		if(sim_main_thread_sleep_till > 0 && sim_plugin_sched_thread_sleep_till > 0) {
			debug2("sim_main_thread_sleep %ld", sim_main_thread_sleep_till-get_sim_utime());
			debug2("sim_plugin_sched_thread_sleep %ld", sim_plugin_sched_thread_sleep_till-get_sim_utime());
			skipping_to_utime = sim_main_thread_sleep_till;
			skipping_to_utime = MIN(skipping_to_utime, sim_plugin_sched_thread_sleep_till);
			skipping_to_utime = MIN(skipping_to_utime, sim_next_event->when);
			now = get_sim_utime();
			skip_usec = skipping_to_utime - now - real_sleep_usec;
			if( skip_usec > real_sleep_usec ) {
				debug2("skipping %ld usec", skip_usec);
				set_sim_time(now + skip_usec);
			}
		}*/


		/*exit if everything is done*/

		if(sim_next_event==sim_last_event &&
				sim_first_active_job==NULL &&
				slurmctld_diag_stats.jobs_running + slurmctld_diag_stats.jobs_pending == 0 &&
				slurm_sim_conf->time_after_all_events_done >=0) {
			/* no more jobs to submit */
			_update_diag_job_state_counts();
			if(slurmctld_diag_stats.jobs_running + slurmctld_diag_stats.jobs_pending == 0){
				if(all_done==0) {
					info("All done exit in %.3f seconds", slurm_sim_conf->time_after_all_events_done/1000000.0);
					all_done = get_sim_utime() + slurm_sim_conf->time_after_all_events_done;
				}
				now = get_sim_utime();
				if(all_done - now < 0) {
					info("All done.");
					raise(SIGINT);
				}
			} else {
				all_done=0;
			}


		}
		/* SIM End */
	}
	xfree(stmp1);
	xfree(stmp2);
}

void create_sim_events_handler ()
{
	slurm_thread_create(&thread_id_event_thread,
			sim_events_thread, NULL);
}


int
main (int argc, char **argv)
{
    int64_t sim_slurmctld_main_start_time = get_real_utime();

	daemonize = 0;
	info("Starting Slurm Simulator");

	//sim_init_slurmd(argc, argv);


	// correct for simulator init time
	//simulator_start_time += (sim_slurmctld_main_start_time - sim_constructor_start_time);
	info("process_create_time_real: %" PRId64, process_create_time_real);
    info("sim_constructor_start_time: %" PRId64, sim_constructor_start_time);
	info("sim_slurmctld_main_start_time: %" PRId64, sim_slurmctld_main_start_time);


	int slurmctld_argc=0;
	char **slurmctld_argv = 0;
	sim_slurmctld_parse_commandline(&slurmctld_argc, &slurmctld_argv, argc, argv);

	sim_init_events();
	sim_print_events();

	create_sim_events_handler();

	int64_t sim_slurmctld_main_start_time2 = get_real_utime();
	//simulator_start_time += (sim_slurmctld_main_start_time2 - sim_constructor_start_time);
	info("sim_slurmctld_main_start_time2: %" PRId64, sim_slurmctld_main_start_time2);
	info("simulator_start_time(corrected): %" PRId64, simulator_start_time);
	slurmctld_main(slurmctld_argc, slurmctld_argv);

	debug("%d", controller_sigarray[0]);
}


