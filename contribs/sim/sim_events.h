#ifndef _SIM_EVENTS_H
#define _SIM_EVENTS_H

#include <stdint.h>

/******************************************************************************
 * Simulation Events
 ******************************************************************************/
typedef enum {
	SIM_NODE_REGISTRATION = 1001,
	SIM_SUBMIT_BATCH_JOB,
	SIM_COMPLETE_BATCH_SCRIPT,
	SIM_EPILOG_COMPLETE,
	SIM_CANCEL_JOB,
} sim_event_type_t;

typedef struct sim_event_submit_batch_job {
	int wall_utime; /*actual walltime*/
	uint32_t job_id;	/* job ID */
	char **argv;
	int argc;
} sim_event_submit_batch_job_t;

typedef struct sim_event {
	int64_t when; /* time of event in usec*/
	struct sim_event *next;
	struct sim_event *previous;
	sim_event_type_t type; /* event type */
	void *payload; /* event type */
} sim_event_t;

extern sim_event_t * sim_next_event;

extern void sim_init_events();
extern void sim_print_events();
extern void sim_print_event(sim_event_t * event);

extern void sim_insert_event(int64_t when, int type, void *payload);
extern void sim_insert_event_comp_job(uint32_t job_id);
extern void sim_insert_event_epilog_complete(uint32_t job_id);
extern void sim_job_requested_kill_timelimit(uint32_t job_id);

extern pthread_mutex_t events_mutex;

#endif

