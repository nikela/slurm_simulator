#include "slurm/slurm.h"

#include "../../src/common/xmalloc.h"
#include "../../src/common/xstring.h"

#include "../../src/common/log.h"

#include "../../contribs/sim/sim_time.h"
#include "../../contribs/sim/sim_conf.h"
#include "../../contribs/sim/sim_events.h"
#include "../../contribs/sim/sim_jobs.h"
#include "../../contribs/sim/sim.h"

//typedef struct sim_event {
//	int64_t when; /* time of event in usec*/
//	struct sim_event *next;
//	struct sim_event *previous;
//	int type; /* event type */
//	void *payload; /* event type */
//
//} sim_event_t;

extern int64_t simulator_start_time;

pthread_mutex_t events_mutex = PTHREAD_MUTEX_INITIALIZER;

sim_event_t * sim_first_event = NULL;
sim_event_t * sim_last_event = NULL;
sim_event_t * sim_next_event = NULL;

int sim_n_noncyclic_events = 0;
int sim_n_cyclic_events = 0;

void sim_insert_event2(sim_event_t * event)
{
	pthread_mutex_lock(&events_mutex);
	sim_event_t * following_event=sim_next_event;
	/* here "<=" is important, this allows for events with same time to be queued based on their arrival*/
	while(following_event->when <= event->when) {
		following_event = following_event->next;
	}

	event->previous = following_event->previous;
	event->next = following_event;
	event->previous->next = event;
	following_event->previous = event;

	if(event->when < sim_next_event->when) {
		sim_next_event = event;
	}
    switch(event->type) {
        case SIM_TIME_ZERO:
            break;
        case SIM_TIME_INF:
            break;
        //case SIM_RUN_BACKFILL_SCHEDULER:
		case SIM_PRIORITY_DECAY:
		case SIM_SET_DB_INDEX:
            sim_n_cyclic_events++;
            break;
        default:
            sim_n_noncyclic_events++;
            break;
    }
	pthread_mutex_unlock(&events_mutex);
}

extern sim_event_t * sim_pop_next_event()
{
    sim_event_t * event = sim_next_event;

    pthread_mutex_lock(&events_mutex);
    sim_next_event = sim_next_event->next;
    pthread_mutex_unlock(&events_mutex);

    switch(event->type) {
        case SIM_TIME_ZERO:
            break;
        case SIM_TIME_INF:
            break;
        //case SIM_RUN_BACKFILL_SCHEDULER:
		case SIM_PRIORITY_DECAY:
		case SIM_SET_DB_INDEX:
            sim_n_cyclic_events--;
            break;
        default:
            sim_n_noncyclic_events--;
            break;
    }
    if(sim_n_noncyclic_events<0){
        fatal("Removed move events than where added!");
    }
    if(sim_n_cyclic_events<0){
        fatal("Removed move events than where added!");
    }
    return event;
}

void sim_insert_event(int64_t when, int type, void *payload)
{
	sim_event_t * event = xcalloc(1,sizeof(*event));
	event->when = when;
	event->type = type;
	event->payload = payload;

	sim_insert_event2(event);
}

extern void sim_print_event(sim_event_t * event)
{
	int i;
	char *str=NULL;
	sim_event_submit_batch_job_t *payload=NULL;

	switch(event->type) {
	case SIM_NODE_REGISTRATION:
		info("%" PRId64 "\t SIM_NODE_REGISTRATION", event->when);
		break;
	case SIM_SUBMIT_BATCH_JOB:
		payload = (sim_event_submit_batch_job_t*)event->payload;
		for(i=0;i<payload->argc;++i) {
			xstrcat(str, payload->argv[i]);
			xstrcat(str, " ");
		}
		info("%" PRId64 "\tSIM_SUBMIT_BATCH_JOB --jid %d --sim-walltime %" PRId64 " %s",
				event->when, payload->job_id, payload->wall_utime, str);
		str[0]='\0';
		break;
	default:
		info("%" PRId64 "\t%d", event->when, event->type);
		break;
	}
	xfree(str);
}
extern void sim_print_events()
{
	info("Simulation Events:");
	sim_event_t * event=sim_first_event;
	while(event != NULL) {
		sim_print_event(event);
		event = event->next;
	}
	info("End Simulation Events:");
}

extern void split_cmd_line(const char * cmd_line, char ***argv, int *argc)
{
	int cmd_line_len = strlen(cmd_line);
	int m_argc = 0;
	/* extra padding for job name */
	int m_argc_padding = 4;
	char **m_argv=NULL;
	char *m_cmd_line;

	bool in_arg_mode=true;
	int i,m_i;

	m_cmd_line = xmalloc(cmd_line_len*sizeof(char));
	strncpy(m_cmd_line, cmd_line, cmd_line_len);

	// first pass: count args,
	// make m_cmd_line where all whitespaces are combined and replaced  with \0
	// and " is handled too

	i = 0;
	while(cmd_line[i]==' ' || cmd_line[i]=='\t') {
		++i;
	}
	m_i = 0;
	for (; i < cmd_line_len; ++i) {
		if(!in_arg_mode && (cmd_line[i]==' ' || cmd_line[i]=='\t')) {
			while((cmd_line[i]==' ' || cmd_line[i]=='\t') && i<cmd_line_len) {
				++i;
			}
			in_arg_mode = true;
			m_cmd_line[m_i]='\0';
			++m_i;
		}
		if(in_arg_mode) {
			while(cmd_line[i]!=' ' && cmd_line[i]!='\t' && i<cmd_line_len) {
				// handle escape
				if(i<cmd_line_len && cmd_line[i]=='"') {
					++i;
					while(cmd_line[i]!='"' && i<cmd_line_len) {
						if(cmd_line[i]!='\n'){
							m_cmd_line[m_i] = cmd_line[i];
							++m_i;
						}
						++i;
					}
				} else {
					if(cmd_line[i]!='\n'){
						m_cmd_line[m_i] = cmd_line[i];
						++m_i;
					}
				}
				++i;
			}
			in_arg_mode=false;
			++m_argc;
			--i;
		}
	}
	m_cmd_line[m_i]='\0';
	int m_cmd_line_len = m_i;

	// second pass set argv
	m_argv = (char**)xmalloc((m_argc+m_argc_padding)*sizeof(char**));

	in_arg_mode = true;
	m_argv[0]=m_cmd_line;
	m_argc = 1;
	for (m_i = 0; m_i < m_cmd_line_len-1; ++m_i) {
		if(m_cmd_line[m_i]=='\0') {
			m_argv[m_argc]=m_cmd_line+m_i+1;
			++m_argc;
		}
	}

	*argc = m_argc;
	*argv = m_argv;
}

void* sim_submit_batch_job_get_payload(char *event_details)
{
	sim_event_submit_batch_job_t *payload = xcalloc(1,sizeof(sim_event_submit_batch_job_t));

	int iarg, argc;
	char **argv;
	char *s_job_id = NULL;
	char *username=NULL;
	char *sleep_time_str=NULL;
	char *job_name;
	int job_name_set = 0;
	int workdir_set = 0;
	int sleep_set = 0;
	int pseudo_job_set = 0;
	//char job_name_str[64];

	split_cmd_line(event_details,&argv,&argc);

	payload->argv = xcalloc(argc + 10, sizeof(char*));
	payload->argv[0] = xstrdup("sbatch");
	payload->argc = 1;
	payload->wall_utime = -1;

	//scan for values:
	for(iarg=0;iarg<argc;++iarg){
		if(xstrcmp(argv[iarg], "-jid")==0 && iarg+1<argc){
			++iarg;
			payload->job_id = atoi(argv[iarg]);
			s_job_id = argv[iarg];
			error("Don't set -jid!");
			exit(1);
		} else if(xstrcmp(argv[iarg], "-J")==0 && iarg+1<argc){
			++iarg;
			job_name = argv[iarg];
			job_name_set = 1;
		} else if(xstrncmp(argv[iarg], "--job-name=", 11)==0){
			job_name = xstrchr(argv[iarg],'=')+1;
			job_name_set = 1;
		} else if(xstrcmp(argv[iarg], "-D")==0 && iarg+1<argc){
			// -D, --chdir=directory       set working directory for batch script\n"
			++iarg;
			workdir_set = 1;
		} else if(xstrncmp(argv[iarg], "--chdir=", 8)==0){
			// -D, --chdir=directory       set working directory for batch script\n"
			++iarg;
			workdir_set = 1;
		} else if(xstrncmp(argv[iarg], "--uid=", 6)==0 && iarg+1<argc){
			// --uid=user_id           user ID to run job as (user root only)\n"
			username = xstrchr(argv[iarg],'=')+1;
		} else if(xstrcmp(argv[iarg], "-sim-walltime")==0 && iarg+1<argc){
			++iarg;
			sleep_time_str = argv[iarg];
			payload->wall_utime = (int64_t)atof(argv[iarg])*1000000;

		} else if(xstrcmp(argv[iarg], "pseudo.job")==0){
			pseudo_job_set = 1;

		} else if(xstrcmp(argv[iarg], "-sleep")==0 && iarg+1<argc){
			++iarg;
			if(pseudo_job_set==0) {
				error("-sleep should be after pseudo.job");
			}
			sleep_set = 1;
			sleep_time_str = argv[iarg];
			payload->wall_utime = (int64_t)atof(argv[iarg])*1000000;
		}
	}

	/* set job name for reference */
	if(job_name_set == 0) {
		payload->argv[payload->argc] = xstrdup("-J");
		payload->argc += 1;

		payload->argv[payload->argc] = xstrdup_printf("jobid_%s", s_job_id);
		payload->argc += 1;

		error("Set job names to jobid_<integer>!");
		exit(1);
	}
	if(xstrncmp(job_name, "jobid_", 6)!=0) {
		error("Set job names to jobid_<integer>!");
		exit(1);
	}

	payload->job_id = 0;
	payload->job_sim_id = get_job_sim_id(job_name);

	/* set workdir */
	if(workdir_set == 0) {
		if(username != NULL) {
			payload->argv[payload->argc] = xstrdup_printf("--chdir=/home/%s", username);
		} else {
			payload->argv[payload->argc] = xstrdup_printf("--chdir=/tmp");
		}
		payload->argc += 1;
	}

	/* set rest of arguments */
	for(iarg=0;iarg<argc;++iarg){
		if(xstrcmp(argv[iarg], "-jid")==0 && iarg+1<argc){
			++iarg;
		} else if(xstrcmp(argv[iarg], "-sim-walltime")==0 && iarg+1<argc){
			++iarg;
		} else if(xstrcmp(argv[iarg], "pseudo.job")==0){
			payload->argv[payload->argc] = xstrdup("/opt/cluster/microapps/pseudo.job");
			payload->argc += 1;

		} else {
			payload->argv[payload->argc] = xstrdup(argv[iarg]);
			payload->argc += 1;
		}
	}
	if(sleep_set==0) {
		payload->argv[payload->argc] = xstrdup("-sleep");
		payload->argc += 1;
		if(sleep_time_str!=NULL) {
			payload->argv[payload->argc] = xstrdup(sleep_time_str);
		} else {
			payload->argv[payload->argc] = xstrdup("-1");
		}
		payload->argc += 1;
	}

	if(payload->wall_utime < 0) {
		// i.e. run till wall_utime limit
		payload->wall_utime = INT64_MAX;
	}

	xfree(argv[0]);
	xfree(argv);
	return payload;
}

int sim_insert_event_by_cmdline(char *cmdline) {
	char * event_command = strtok(cmdline, "|");
	char * event_details = strtok(NULL, "|");

	int event_argc;
	char **event_argv;
	uint64_t start_time=0;
	double dt = -1;

	sim_event_t * event = xcalloc(1,sizeof(sim_event_t));
	event->type=0;

	// parse event type/when

	split_cmd_line(event_command,&event_argv,&event_argc);

	for(int iarg=0;iarg<event_argc;++iarg) {
		if(xstrcmp(event_argv[iarg], "-dt")==0 && iarg+1<event_argc){
			++iarg;
			dt = atof(event_argv[iarg]);
		}
		if(xstrcmp(event_argv[iarg], "-e")==0 && iarg+1<event_argc){
			++iarg;
			if(xstrcmp(event_argv[iarg], "submit_batch_job")==0) {
				event->type = SIM_SUBMIT_BATCH_JOB;
			}
		}
	}

	xfree(event_argv[0]);
	xfree(event_argv);

	if(start_time==0 && dt < 0) {
		error("Start time is not set for %s (set either -t or -dt)", cmdline);
		return -1;
	}
	if(start_time!=0 && dt >= 0) {
		error("Incorrect start time for %s (set either -t or -dt)", cmdline);
		return -1;
	}
	if(event->type == 0) {
		error("Unknown event type for %s (set either -t or -dt)", cmdline);
		return -1;
	}

	if(dt >= 0) {
		event->when = simulator_start_time + \
				slurm_sim_conf->microseconds_before_first_job + \
				(int64_t)(dt * 1000000.) + \
				slurm_sim_conf->first_job_delay;
	}


	// parse event details
	if(event->type == SIM_SUBMIT_BATCH_JOB) {
		event->payload = sim_submit_batch_job_get_payload(event_details);
	}

	//xfree(event_details);
	//xfree(event_command);
	sim_insert_event2(event);
	return 0;
}

void sim_init_events()
{
	// pad events list with small and large time to avoid extra comparison
	sim_first_event=xcalloc(1,sizeof(*sim_first_event));
	sim_first_event->when = 0;
    sim_first_event->type = SIM_TIME_ZERO;
	sim_last_event=xcalloc(1,sizeof(*sim_last_event));
	sim_last_event->when = INT64_MAX;
    sim_last_event->type = SIM_TIME_INF;

	sim_first_event->next = sim_last_event;
	sim_last_event->previous = sim_first_event;

	sim_next_event = sim_first_event;

	// add first node registation
	sim_event_t * event = xcalloc(1,sizeof(sim_event_t));
	event->type=SIM_NODE_REGISTRATION;
	event->when=get_sim_utime();
	sim_insert_event2(event);

	// read events from simulation events file
	FILE *f_in = fopen(slurm_sim_conf->events_file, "rt");
	char *line = NULL;
	size_t len = 0;
	ssize_t read;

	if (f_in == NULL) {
		error("Can not open events file %s!", slurm_sim_conf->events_file);
		exit(1);
	}

	while ((read = getline(&line, &len, f_in)) != -1) {
		int comment = 0;
		int not_white_space=0;
		for(int i=0; i < read;++i) {
			if(!(line[i]==' ' || line[i]=='\t' || line[i]=='\n')) {
				not_white_space += 1;
			}
			if(line[i]=='#' && not_white_space==0) {
				comment = 1;
			}
		}
		if(!comment && not_white_space>0) {
			sim_insert_event_by_cmdline(line);
		}
	}
	fclose(f_in);
}

void sim_insert_event_comp_job(uint32_t job_id)
{
	sim_job_t *sim_job = sim_find_active_sim_job(job_id);
	if(sim_job==NULL) {
		error("Sim:sim_insert_event_comp_job: Can not find job %d among active sim jobs!", job_id);
		return;
	}
	int64_t when;
	const int64_t year=365*24*3600*(int64_t)1000000;
	if(sim_job->start_time == 0){
		pthread_mutex_lock(&active_job_mutex);
		sim_job->start_time = get_sim_utime();
		pthread_mutex_unlock(&active_job_mutex);
	}

	if(sim_job->walltime < year && sim_job->walltime >= 0){
		when = sim_job->start_time + sim_job->walltime;
		//when += slurm_sim_conf->comp_job_delay;
		sim_insert_event(when, SIM_COMPLETE_BATCH_SCRIPT, (void*)sim_job);
	}
}


void sim_job_requested_kill_timelimit(uint32_t job_id)
{
	sim_job_t *sim_job = sim_find_active_sim_job(job_id);

	if(sim_job==NULL) {
		debug2("Sim job %d not found", job_id);
		return;
	}

	if(sim_job->requested_kill_timelimit) {
		return;
	}

	pthread_mutex_lock(&active_job_mutex);
	sim_job->requested_kill_timelimit = 1;
	pthread_mutex_unlock(&active_job_mutex);

	sim_insert_event(get_sim_utime()+slurm_sim_conf->timelimit_delay, SIM_COMPLETE_BATCH_SCRIPT, (void*)sim_job);
}

void sim_insert_event_rpc_epilog_complete(uint32_t job_id)
{
	sim_job_t *sim_job = sim_find_active_sim_job(job_id);
	if(sim_job==NULL) {
		error("Sim:sim_insert_event_rpc_epilog_complete: Can not find job %d among active sim jobs!", job_id);
		return;
	}
	if(sim_job->comp_job) {
		return;
	}
	pthread_mutex_lock(&active_job_mutex);
	sim_job->comp_job = 1;
	pthread_mutex_unlock(&active_job_mutex);

	sim_insert_event(get_sim_utime()+slurm_sim_conf->comp_job_delay, SIM_EPILOG_COMPLETE, (void*)sim_job);
}
