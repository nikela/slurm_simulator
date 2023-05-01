/*
 * this source wrap slurmctld/controller.c during slurmctld building
 * contains main simulated event loop
 */
#include <inttypes.h>
extern int64_t sim_main_thread_sleep_till;
extern int64_t sim_sched_thread_cond_wait_till;
extern int64_t sim_plugin_backfill_thread_sleep_till;
int64_t last_sched_time_slurmctld_background;

#include <pthread.h>
int proc_rec_count=0;
pthread_mutex_t proc_rec_count_lock;

extern void sim_events_loop();

#define main slurmctld_main
#include "../../src/slurmctld/controller.c"
#undef main

#include "../../contribs/sim/sim_time.h"
#include "../../contribs/sim/sim_conf.h"
#include "../../contribs/sim/sim_events.h"
#include "../../contribs/sim/sim_users.h"
#include "../../contribs/sim/sim_jobs.h"
#include "../../contribs/sim/sim_rt_events.h"
#include "../../contribs/sim/sim.h"
#include "../../contribs/sim/slurmctld_sim.h"

#include <inttypes.h>
#include <signal.h>

pthread_t thread_id_event_thread;


extern void submit_job(sim_event_submit_batch_job_t* event_submit_batch_job);
extern int sim_init_slurmd();
extern void sim_rpc_epilog_complete(uint32_t job_id);
extern int sim_registration_engine();
extern bool sim_job_epilog_complete(uint32_t job_id, char *node_name,
                                    uint32_t return_code);
extern void sim_notify_slurmctld_nodes();

void * (*sim_decay_thread_ref)(void *no_data)=NULL;

extern int (*sim_slurmctrld_pthread_create_ref)(pthread_t *newthread,
								   const pthread_attr_t *attr,
								   void *(*start_routine) (void *),
								   void *arg,
								   const char *id,
								   const char *func,
								   const char *sarg,
								   const char *funccall,
								   const char *filename,
								   const char *note,
								   const int line);

int sim_slurmctrld_pthread_create (pthread_t *newthread,
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
	} else if (xstrcmp("&slurmctld_config.thread_id_acct_update", id) == 0) {
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
	} else if (xstrcmp("&decay_handler_thread", id) == 0 && xstrcmp("_decay_thread", func) == 0) {
		debug("sim_pthread_create: %s ... skip.", id);
		// priority/multifactor plugin
		sim_decay_thread_ref = start_routine;
		sim_insert_event( get_sim_utime(),SIM_PRIORITY_DECAY,NULL);
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


/* reference to sched_plugin */
uint64_t (*sim_backfill_agent_ref)(void)=NULL;

extern void (*sim_slurmctrld_cond_broadcast_ref)(pthread_cond_t * cond,
										  const char *scond,
										  const char *filename,
										  const int line,
										  const char *func);

void sim_slurmctrld_cond_broadcast(pthread_cond_t * cond,
		const char *scond,
		const char *filename,
		const int line,
		const char *func)
{
	if (xstrcmp("&slurmctld_config.acct_update_cond", scond) == 0) {
		sim_insert_event(get_sim_utime(),SIM_ACCOUNTING_UPDATE,NULL);
	}
}


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
    bool exit_status;
    int error_code;
	job_record_t *job_ptr = find_job_record(job_id);

	if(job_ptr==NULL){
		error("Can not find record for %d job!", job_id);
		sim_remove_active_sim_job(job_id);
		return;
	}

	slurm_step_id_t step_id = { .job_id = job_ptr->job_id,
						    .step_id = SLURM_BATCH_SCRIPT,
						    .step_het_comp = NO_VAL };
	step_record_t *step_ptr = find_step_record(job_ptr, &step_id);
	if(step_ptr!=NULL) {
		step_ptr->exit_code = 0;
		//jobacctinfo_destroy(step_ptr->jobacct);
		//step_ptr->jobacct = comp_msg->jobacct;
		//comp_msg->jobacct = NULL;
		step_ptr->state |= JOB_COMPLETING;
		jobacct_storage_g_step_complete(acct_db_conn, step_ptr);
		delete_step_record(job_ptr, step_ptr);
	}


	debug2("Processing RPC: REQUEST_COMPLETE_BATCH_SCRIPT from "
		"uid=%u JobId=%u",
		job_ptr->user_id, job_id);

	if(IS_JOB_COMPLETING(job_ptr)){
		sim_rpc_epilog_complete(job_ptr->job_id);
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
    error_code = job_complete(job_ptr->job_id, job_ptr->user_id, false, false, SLURM_SUCCESS);
    unlock_slurmctld(job_write_lock1);

    if(error_code==SLURM_SUCCESS){
        exit_status = sim_job_epilog_complete(job_ptr->job_id, "localhost", SLURM_SUCCESS);
        if (exit_status){
            // @todo proper handling whould include sending REQUEST_TERMINATE_JOB
            // here we skipping it
            sim_remove_active_sim_job(job_id);
            // agent after sending REQUEST_TERMINATE_JOB will ask to run scheduler
            sim_notify_slurmctld_nodes();
        } else {
            sim_insert_event_rpc_epilog_complete(job_id);
        }
    } else {
        sim_insert_event_rpc_epilog_complete(job_id);
    }
}


void sim_rpc_epilog_complete(uint32_t job_id)
{
    //char *hostname;
    bool run_scheduler = false;
    bool defer_sched = (xstrcasestr(slurm_conf.sched_params, "defer"));

    job_record_t *job_ptr = find_job_record(job_id);

    if(job_ptr==NULL){
        error("Can not find record for %d job!", job_id);
        sim_remove_active_sim_job(job_id);
        return;
    }


    slurmctld_lock_t job_write_lock = {
            NO_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK, READ_LOCK };

    if(IS_JOB_COMPLETING(job_ptr)){
        lock_slurmctld(job_write_lock);
        if (job_epilog_complete(job_ptr->job_id, "localhost", SLURM_SUCCESS))
            run_scheduler = true;
        unlock_slurmctld(job_write_lock);

        if (run_scheduler) {
            /*
             * In defer mode, avoid triggering the scheduler logic
             * for every epilog complete message.
             * As one epilog message is sent from every node of each
             * job at termination, the number of simultaneous schedule
             * calls can be very high for large machine or large number
             * of managed jobs.
             */
            if (!LOTS_OF_AGENTS && !defer_sched){
                debug3("Calling schedule from epilog_complete");
                schedule(false);	/* Has own locking */
            }
            else{
                debug3("Calling queue_job_scheduler from epilog_complete");
                queue_job_scheduler();
            }
            schedule_node_save();		/* Has own locking */
            schedule_job_save();		/* Has own locking */
        }
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
    lock_slurmctld(job_write_lock);
    if (job_epilog_complete(job_ptr->job_id, "localhost", SLURM_SUCCESS))
        run_scheduler = true;
    unlock_slurmctld(job_write_lock);

    if (run_scheduler) {
        /*
         * In defer mode, avoid triggering the scheduler logic
         * for every epilog complete message.
         * As one epilog message is sent from every node of each
         * job at termination, the number of simultaneous schedule
         * calls can be very high for large machine or large number
         * of managed jobs.
         */
        if (!LOTS_OF_AGENTS && !defer_sched){
            debug3("Calling schedule from epilog_complete");
            schedule(false);	/* Has own locking */
        }
        else{
            debug3("Calling queue_job_scheduler from epilog_complete");
            queue_job_scheduler();
        }
        schedule_node_save();		/* Has own locking */
        schedule_job_save();		/* Has own locking */
    }

    //free(hostname);
    sim_remove_active_sim_job(job_id);
}


// 0 means such event currently do not occur
// 1 means expecting such event in feture
// >10 usec time when event started

int64_t rt_events[MAX_RT_EVENT_TYPES];

// time to wait after event is done
int64_t rt_events_post_wait[MAX_RT_EVENT_TYPES];


int _event_expect(slurm_sim_rt_event_t event_type, const char *s_event_type, const char *func, const char *filename, const int line)
{
    //int64_t now=get_sim_utime();
    rt_events[event_type]=1;
	return 0;
}


int _event_started(slurm_sim_rt_event_t event_type, const char *s_event_type, const char *func, const char *filename, const int line)
{
    int64_t now=get_sim_utime();
    rt_events[event_type]=now;

    switch(event_type) {
	  case SUBMIT_JOB_SSIM_RT_EVENT:
		  event_expect(SCHED_SSIM_RT_EVENT);
		  break;
	  case EPILOG_COMPLETE_SSIM_RT_EVENT:
		  event_expect(SCHED_SSIM_RT_EVENT);
		  break;
	  default:
		  break;
    }
	return 0;
}

int _event_ended(slurm_sim_rt_event_t event_type, const char *s_event_type, const char *func, const char *filename, const int line)
{
	int64_t now=get_sim_utime();
	rt_events[event_type]=0;

    switch(event_type) {
	  case SUBMIT_JOB_SSIM_RT_EVENT:
		  rt_events_post_wait[event_type]=now+1000000;
		  break;
	  case EPILOG_COMPLETE_SSIM_RT_EVENT:
		  rt_events_post_wait[event_type]=now+1000000;
	  	  break;
	  default:
	  		  break;
    }
	return 0;
}

void sim_events_first_loop()
{
	//time_t start_time;
	//int jobs_submit_count=0;

	char *stmp1 = xcalloc(128, sizeof(char));
	char *stmp2 = xcalloc(128, sizeof(char));

	int64_t cur_real_utime, cur_sim_utime;
	//int64_t slurmctld_diag_stats_lastcheck;
	//int64_t last_event_usec=0;

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
	xfree(stmp1);
	xfree(stmp2);

	/* add initial events */
	sim_schedule_plugin_run_once();
}

void sim_events_loop()
{
	static bool first_run = true;

	if(first_run) {
		sim_events_first_loop();
		first_run = false;
	}
	static sim_event_t * event = NULL;
	static int64_t all_done=0;

	static int scaling_on=0;
	int64_t now;
	int64_t cur_real_utime;

	now = get_sim_utime();
	cur_real_utime = get_real_utime();
	//start_time = now;
	if(scaling_on==0 && cur_real_utime-process_create_time_real>10000000) {
		set_sim_time_scale(slurm_sim_conf->clock_scaling);
		scaling_on=1;
	}

	/* SIM Start */
	if(sim_next_event->when - now < 0) {
		while(sim_next_event->when - now < 0) {
			event = sim_pop_next_event();

			sim_print_event(event);
			debug2("%s: sim_n_noncyclic_events=%d sim_n_cyclic_events=%d",__func__ ,sim_n_noncyclic_events,sim_n_cyclic_events);

			switch(event->type) {
			case SIM_NODE_REGISTRATION:
				event_started(GENERAL_SSIM_RT_EVENT);
				sim_registration_engine();
				event_ended(GENERAL_SSIM_RT_EVENT);
				break;
			case SIM_SUBMIT_BATCH_JOB:
				event_started(SUBMIT_JOB_SSIM_RT_EVENT);
				submit_job((sim_event_submit_batch_job_t*)event->payload);
				event_ended(SUBMIT_JOB_SSIM_RT_EVENT);
				break;
			case SIM_COMPLETE_BATCH_SCRIPT:
				event_started(GENERAL_SSIM_RT_EVENT);
				sim_complete_job(((sim_job_t*)event->payload)->job_id);
				event_ended(GENERAL_SSIM_RT_EVENT);
				break;
			case SIM_EPILOG_COMPLETE:
				event_started(EPILOG_COMPLETE_SSIM_RT_EVENT);
				sim_rpc_epilog_complete(((sim_job_t*)event->payload)->job_id);
				event_ended(EPILOG_COMPLETE_SSIM_RT_EVENT);
			case SIM_RUN_BACKFILL_SCHEDULER:
				sim_schedule_plugin_run_once();
				break;
			case SIM_ACCOUNTING_UPDATE:
				// mimicking _acct_update_thread
				(void) list_delete_all(slurmctld_config.acct_update_list,
									   _acct_update_list_for_each,
									   NULL);
				break;
			case SIM_PRIORITY_DECAY:
				if(sim_decay_thread_ref!=NULL) {
					(*sim_decay_thread_ref)(NULL);
				}
				sim_insert_event(event->when + slurm_conf.priority_calc_period*USEC_IN_SEC, SIM_PRIORITY_DECAY,NULL);
				break;
			default:
				break;
			}
			//last_event_usec = get_sim_utime();

		}
		//
		//jobs_submit_count++;
	}
	// run main scheduler if needed
	sim_sched_agent_loop();


	/*check can we skip some time*/
	int64_t skipping_to_utime = INT64_MAX;
	int64_t skip_usec;
	if(sim_main_thread_sleep_till > 0) {
		skipping_to_utime = sim_main_thread_sleep_till;
		skipping_to_utime = MIN(skipping_to_utime, sim_plugin_backfill_thread_sleep_till);
		//skipping_to_utime = MIN(skipping_to_utime, sim_sched_thread_cond_wait_till);
		skipping_to_utime = MIN(skipping_to_utime, sim_next_event->when);
		skipping_to_utime = MIN(skipping_to_utime, sim_thread_priority_multifactor_sleep_till);
		skipping_to_utime = MIN(skipping_to_utime, sim_agent_init_sleep_till);
		skipping_to_utime = MIN(skipping_to_utime, sim_sched_thread_cond_wait_till);
		if(job_sched_cnt>0) {
			skipping_to_utime = MIN(skipping_to_utime, last_sched_time_slurmctld_background+batch_sched_delay*1000000);
		}

		now = get_sim_utime();



		if(((cur_real_utime-process_create_time_real)>10000000) && (all_done==0) && (proc_rec_count==0)) {
			//&&
			//	((now-last_event_usec) > 1000000)
			// sim_main_thread_sleep_till > 0 i.e. in sleep within slurmctrld_backgroung
			//     job_sched_cnt can not be reset to 0 and call schedule right now

			// sim_sched_thread_sleep_till > 0 &&
			// mainthread kick sim_plugin_sched_thread_sleep_till

			// main thread is slepping (run walllimit check) and backfiller is sleeping
			//debug2("sim_main_thread_sleep %ld", sim_main_thread_sleep_till-get_sim_utime());
			//debug2("sim_plugin_sched_thread_sleep %ld", sim_sched_thread_sleep_till-get_sim_utime());


			skip_usec = skipping_to_utime - now - 10000;
			if( skip_usec > real_sleep_usec ) {
				if(skip_usec > 1000000) {
					skip_usec = 1000000;
				}
				if(skip_usec > 10000) {
					//debug2("skipping %" PRId64 " usec %.3f from last event", skip_usec,(now-last_event_usec)/1000000.0);
					set_sim_time(now + skip_usec);
					//now = get_sim_utime();
				}
			}
		}
	}
//		else {
//			debug2("NotSkipping %d %d %d %d %d %d",
//					cur_real_utime-process_create_time_real>10000000,
//					all_done==0,
//					sim_main_thread_sleep_till > 0,
//					sim_sched_thread_cond_wait_till > 0,
//					sim_plugin_backfill_thread_sleep_till > 0,
//					sim_next_event->when > now);
//		}



	/*exit if everything is done*/

	if(sim_n_noncyclic_events<=0 &&
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
				//raise(SIGINT);
				exit(0);
			}
		} else {
			all_done=0;
		}


	}
}

/* execure scheduler from schedule_plugin */
extern void sim_schedule_plugin_run_once()
{
	//int backfill_was_ran=0;

	/*double t=get_realtime();
	struct tms m_tms0,m_tms1;

	clock_t st=clock();
	times(&m_tms0);*/
	uint64_t next_backfill_utime;
	if (sim_backfill_agent_ref!=NULL){
		//backfill_was_ran=
		next_backfill_utime=(*sim_backfill_agent_ref)();
	} else {
		info("Error: sched_plugin do not support simulator");
		next_backfill_utime = get_sim_utime()+1000000;
	}
	sim_plugin_backfill_thread_sleep_till = next_backfill_utime;
	sim_insert_event(next_backfill_utime, SIM_RUN_BACKFILL_SCHEDULER, NULL);
	/*times(&m_tms1);
	st=clock()-st;
	t=get_realtime()-t;

	clock_t tms_utime=m_tms1.tms_utime-m_tms0.tms_utime;
	clock_t tms_stime=m_tms1.tms_stime-m_tms0.tms_stime;

	if(backfill_was_ran && slurm_sim_conf->sim_stat!=NULL){
		int jobs_pending=0;
		int jobs_running=0;

		int nodes_idle=0;
		int nodes_mixed=0;
		int nodes_allocated=0;

		if(slurm_sim_conf->sim_stat!=NULL){
			ListIterator job_iterator;
			struct job_record *job_ptr;


			job_iterator = list_iterator_create(job_list);
			while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
				if(IS_JOB_PENDING(job_ptr))jobs_pending++;
				if(IS_JOB_RUNNING(job_ptr))jobs_running++;
			}
			list_iterator_destroy(job_iterator);

			int inx;
			struct node_record *node_ptr = node_record_table_ptr;
			for (inx = 0; inx < node_record_count; inx++, node_ptr++) {
				if(IS_NODE_IDLE(node_ptr))nodes_idle++;
				if(IS_NODE_MIXED(node_ptr))nodes_mixed++;
				if(IS_NODE_ALLOCATED(node_ptr))nodes_allocated++;
			}
		}


		FILE *fout=fopen(slurm_sim_conf->sim_stat,"at");
		if(fout==NULL)
			return;

		time_t now = time(NULL);

		fprintf(fout, "*Backfill*Stats****************************************\n");
		fprintf(fout, "Output time: %s", ctime(&now));
		fprintf(fout, "\tLast cycle when: %s", ctime(&slurmctld_diag_stats.bf_when_last_cycle));
		fprintf(fout, "\tLast cycle: %u\n", slurmctld_diag_stats.bf_cycle_last);
		fprintf(fout, "\tLast depth cycle: %u\n", slurmctld_diag_stats.bf_last_depth);
		fprintf(fout, "\tLast depth cycle (try sched): %u\n", slurmctld_diag_stats.bf_last_depth_try);
		fprintf(fout, "\tLast queue length: %u\n", slurmctld_diag_stats.bf_queue_len);
		fprintf(fout, "\tLast backfilled jobs: %u\n", slurmctld_diag_stats.last_backfilled_jobs);

		fprintf(fout, "\tRun real time: %.6f\n", t);
		fprintf(fout, "\tRun real utime: %ld\n",tms_utime);
		fprintf(fout, "\tRun real stime: %ld\n",tms_stime);
		fprintf(fout, "\tCLK_TCK: %ld\n", sysconf (_SC_CLK_TCK));
		fprintf(fout, "\tRun clock: %ld\n",st);
		fprintf(fout, "\tCLOCKS_PER_SEC: %ld\n",CLOCKS_PER_SEC);

		fprintf(fout, "\tjobs_pending: %d\n",jobs_pending);
		fprintf(fout, "\tjobs_running: %d\n",jobs_running);
		fprintf(fout, "\tnodes_idle: %d\n",nodes_idle);
		fprintf(fout, "\tnodes_mixed: %d\n",nodes_mixed);
		fprintf(fout, "\tnodes_allocated: %d\n",nodes_allocated);

		fclose(fout);
	}
	if(backfill_was_ran && slurm_sim_conf->sdiag_file_out!=NULL){
		sim_sdiag_mini();
	}*/
}

extern void sim_mini_loop(){
/*this function is called from backfiller to let events in main scheduler to process */
}


int
main (int argc, char **argv)
{
    int64_t sim_slurmctld_main_start_time = get_real_utime();

	daemonize = 0;
	info("Starting Slurm Simulator");


	sim_init_slurmd();


	// correct for simulator init time
	//simulator_start_time += (sim_slurmctld_main_start_time - sim_constructor_start_time);
	info("process_create_time_real: %" PRId64, process_create_time_real);
    info("sim_constructor_start_time: %" PRId64, sim_constructor_start_time);
	info("sim_slurmctld_main_start_time: %" PRId64, sim_slurmctld_main_start_time);


	int slurmctld_argc=0;
	char **slurmctld_argv = 0;
	sim_slurmctld_parse_commandline(&slurmctld_argc, &slurmctld_argv, argc, argv);

	sim_init_events();
	sim_slurmctrld_cond_broadcast_ref = sim_slurmctrld_cond_broadcast;
	sim_slurmctrld_pthread_create_ref = sim_slurmctrld_pthread_create;

	print_sim_conf();
	sim_print_users();
	sim_print_events();

	int64_t sim_slurmctld_main_start_time2 = get_real_utime();
	//simulator_start_time += (sim_slurmctld_main_start_time2 - sim_constructor_start_time);
	info("sim_slurmctld_main_start_time2: %" PRId64, sim_slurmctld_main_start_time2);
	info("simulator_start_time(corrected): %" PRId64, simulator_start_time);

	for(int i=0;i<MAX_RT_EVENT_TYPES;i++) {
		rt_events[i]=0;
		rt_events_post_wait[i]=0;
	}
	slurmctld_main(slurmctld_argc, slurmctld_argv);

	debug("%d", controller_sigarray[0]);
}


