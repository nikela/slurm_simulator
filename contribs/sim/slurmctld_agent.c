typedef struct agent_arg agent_arg_t;

#define agent_queue_request slurmctld_agent_queue_request
#include "../../src/slurmctld/agent.c"
#undef agent_queue_request


#include "../../contribs/sim/sim_time.h"
#include "../../contribs/sim/sim_conf.h"
#include "../../contribs/sim/sim_events.h"
#include "../../contribs/sim/sim_jobs.h"
#include "../../contribs/sim/sim.h"

extern void sim_complete_job(uint32_t job_id);

void sim_epilog_complete(uint32_t job_id)
{
	//char *hostname;
	bool defer_sched = (xstrcasestr(slurm_conf.sched_params, "defer"));

	job_record_t *job_ptr = find_job_record(job_id);
	slurmctld_lock_t job_write_lock = {
			NO_LOCK, WRITE_LOCK, WRITE_LOCK, NO_LOCK, READ_LOCK };
	if(job_ptr==NULL){
		error("Can not find record for %d job!", job_id);
		sim_remove_active_sim_job(job_id);
		return;
	}

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


// this wrap actual agent_queue_request
// handle faken requests
void agent_queue_request(agent_arg_t *agent_arg_ptr)
{
	bool call_slurmctld_agent_queue_request=true;
	kill_job_msg_t * kill_job;
	batch_job_launch_msg_t *launch_msg_ptr;
//	job_record_t *job_ptr;
//	time_t now;
	//queued_request_t *queued_req_ptr = NULL;
	//__real_agent_queue_request(agent_arg_ptr);
	//return;

	debug("Sim: __wrap_agent_queue_request msg_type=%s", rpc_num2string(agent_arg_ptr->msg_type));
	//__real_agent_queue_request(agent_arg_ptr);

	switch(agent_arg_ptr->msg_type) {
	case REQUEST_BATCH_JOB_LAUNCH:
		launch_msg_ptr = (batch_job_launch_msg_t *)agent_arg_ptr->msg_args;
		sim_insert_event_comp_job(launch_msg_ptr->job_id);
		call_slurmctld_agent_queue_request=false;
		break;
	case REQUEST_KILL_TIMELIMIT:
		kill_job = (kill_job_msg_t*)agent_arg_ptr->msg_args;
		// Previously commented: complete_job(kill_job->job_id);
		sim_job_requested_kill_timelimit(kill_job->step_id.job_id);
		call_slurmctld_agent_queue_request=false;
		break;
	case REQUEST_TERMINATE_JOB:
		// this initiated from job_compleate by jobs finishing by themselves
		call_slurmctld_agent_queue_request=false;
		break;
	case REQUEST_NODE_REGISTRATION_STATUS:
		debug("Sim: __wrap_agent_queue_request msg_type=%s", rpc_num2string(agent_arg_ptr->msg_type));
		call_slurmctld_agent_queue_request=false;
		break;
	case REQUEST_PARTITION_INFO:
		call_slurmctld_agent_queue_request=true;
		break;
	case REQUEST_NODE_INFO:
		call_slurmctld_agent_queue_request=true;
		break;
	case REQUEST_SUBMIT_BATCH_JOB:
		call_slurmctld_agent_queue_request=true;
		break;
	default:
		error("Sim: unknown for sim request will use normal slurm (msg_type=%s)", rpc_num2string(agent_arg_ptr->msg_type));
		call_slurmctld_agent_queue_request=true;
		break;
	}

	if(call_slurmctld_agent_queue_request) {
		slurmctld_agent_queue_request(agent_arg_ptr);
	}
}
