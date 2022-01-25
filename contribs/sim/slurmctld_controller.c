/*
 * this source wrap slurmctld/controller.c during slurmctld building
 */

#define main slurmctld_main
#include "../../src/slurmctld/controller.c"
#undef main

#include "../../contribs/sim/sim_time.h"

#include <inttypes.h>

int
main (int argc, char **argv)
{
    int64_t sim_slurmctld_main_start_time = get_real_utime();
	int64_t sim_slurmctld_start_time = get_process_create_time();

	daemonize = 0;
	info("Starting Slurm Simulator");

	//sim_init_slurmd(argc, argv);


	// correct for simulator init time
	//simulator_start_time += (sim_slurmctld_main_start_time - sim_constructor_start_time);
	//printf("process_create_time_real: %ld\n", process_create_time_real);
	//printf("sim_constructor_start_time: %ld\n", sim_constructor_start_time);
	info("sim_slurmctld_start_time: %" PRId64, sim_slurmctld_start_time);
	info("sim_slurmctld_main_start_time: %" PRId64, sim_slurmctld_main_start_time);
	info("diff: %ld\n", sim_slurmctld_main_start_time-sim_slurmctld_start_time);

	//sim_init_events();
	//sim_print_events();

	//create_sim_events_handler();


	slurmctld_main(argc, argv);

	debug("%d", controller_sigarray[0]);
}
