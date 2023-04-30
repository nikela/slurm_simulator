#ifndef _SLURMCTRLD_TIME_H
#define _SLURMCTRLD_TIME_H

/* functions declarations used in simulated slurm controller */
#include <stdint.h>

/* simulate a single loop of _sched_agent
 * return true if run scheduler*/
extern bool sim_sched_agent_loop();

#endif