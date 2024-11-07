# Makefile for job_submit/carbon_logging plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) -I$(top_srcdir)/src/common $(JSON_CPPFLAGS) $(LIBCURL_CPPFLAGS)

pkglib_LTLIBRARIES = job_submit_carbon_logging.la

# Job submit carbon logging plugin.
job_submit_carbon_logging_la_SOURCES = job_submit_carbon_logging.c
job_submit_carbon_logging_la_LDFLAGS = $(PLUGIN_FLAGS) $(JSON_LDFLAGS) $(LIBCURL)
# add CURL and JSON-C
job_submit_logging_la_LIBADD = $(LIBCURL) $(JSON_LDFLAGS)

force:
$(job_submit_logging_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`



