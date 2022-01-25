/*
 * this source wrap slurmctld/controller.c during slurmctld building
 * contains main simulated event loop
 */

#define main slurmctld_main
#include "../../src/slurmctld/controller.c"
#undef main

#include "../../contribs/sim/sim_time.h"
#include "../../contribs/sim/sim_conf.h"
#include "../../contribs/sim/sim.h"

#include <inttypes.h>


/*
 * read and remove simulation related arguments
 */
static void sim_slurmctld_parse_commandline(int *new_argc, char ***new_argv, int argc, char **argv)
{
	int i = 0;
	int c = 0;
	int prev_proc_index = -1;
	char *tmp_char;
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
		} else {
			m_argv[m_argc] = xstrdup(argv[i]);
			m_argc += 1;
		}
	}
	*new_argc=m_argc;
	*new_argv=m_argv;
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

	//sim_init_events();
	//sim_print_events();

	//create_sim_events_handler();

	slurmctld_main(slurmctld_argc, slurmctld_argv);

	debug("%d", controller_sigarray[0]);
}
