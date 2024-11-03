/*****************************************************************************\
 *  job_submit_logging.c - Log job submit request specifications.
 *****************************************************************************
 *  Copyright (C) 2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  In addition, as a special exception, the copyright holders give permission
 *  to link the code of portions of this program with the OpenSSL library under
 *  certain conditions as described in each individual source file, and
 *  distribute linked combinations including the two. You must obey the GNU
 *  General Public License in all respects for all of the code used other than
 *  OpenSSL. If you modify file(s) with this exception, you may extend this
 *  exception to your version of the file(s), but you are not obligated to do
 *  so. If you do not wish to do so, delete this exception statement from your
 *  version.  If you delete this exception statement from all source files in
 *  the program, then also delete it here.
 *
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <regex.h>
#include <curl/curl.h>
#include <time.h>

#include "config.h"

#if HAVE_JSON_C_INC
#include <json-c/json.h>
#else
#include <json/json.h>
#endif

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/slurm_xlator.h"
#include "src/slurmctld/slurmctld.h"

/*
 * These variables are required by the generic plugin interface.  If they
 * are not found in the plugin, the plugin loader will ignore it.
 *
 * plugin_name - a string giving a human-readable description of the
 * plugin.  There is no maximum length, but the symbol must refer to
 * a valid string.
 *
 * plugin_type - a string suggesting the type of the plugin or its
 * applicability to a particular form of data or method of data handling.
 * If the low-level plugin API is used, the contents of this string are
 * unimportant and may be anything.  Slurm uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "auth" for Slurm authentication) and <method> is a
 * description of how this plugin satisfies that application.  Slurm will
 * only load authentication plugins if the plugin_type string has a prefix
 * of "auth/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[]       	= "Job submit logging plugin";
const char plugin_type[]       	= "job_submit/logging";
const uint32_t plugin_version   = SLURM_VERSION_NUMBER;

// fixed value in SCI
const uint32_t carbon_intensity = 4; // gCO2/kWh a default value
const uint32_t power_GPU = 300; // W
const uint32_t power_CPU = 220; // W
const uint32_t embodied_CPU = 15*1000; // g
const uint32_t embodied_GPU = 25*1000; // g
const uint32_t total_time = 4*365*24*60; // min
const uint32_t total_cpus = 8;

#define TIME_FORMAT "%Y-%m-%dT%H:%M:%SZ"
#define MAX_RECORDS 48 // 24h, 30min per record. It should be no more than 48
#define REGION_ID 16 // Scotland
#define MAX_SCI_VALUE 10000 //

struct http_response {
	char *message;
	size_t size;
};

extern int regex_gpus(char *tres_per_node)
{
	const char *pattern = "gres:gpu:([0-9]+)";
	regex_t regex;
	regmatch_t pmatch[2];
	char match[10];
	int num_gpus = 0;

	if (regcomp(&regex, pattern, REG_EXTENDED) != 0) {
		error("Could not compile regex");
		return 0;
	}

	if (regexec(&regex, tres_per_node, 2, pmatch, 0) == 0) {
		// Extract the matched substring for GPU count
		int len = pmatch[1].rm_eo - pmatch[1].rm_so;
		strncpy(match, tres_per_node + pmatch[1].rm_so, len);
		match[len] = '\0';
		num_gpus = atoi(match);
	} else {
		error("No match found");
	}

	regfree(&regex);

	return num_gpus;
}

void get_iso8601_time(char *buffer, size_t buffer_size) {
	time_t now = time(NULL);
	struct tm *tm_info = gmtime(&now);
	strftime(buffer, buffer_size, TIME_FORMAT, tm_info);
}

/* Callback to handle the HTTP response */
static size_t _write_callback(void *contents, size_t size, size_t nmemb, void *userp)
{
	size_t realsize = size * nmemb;
	struct http_response *mem = (struct http_response *) userp;

	char *ptr = realloc(mem->message, mem->size + realsize + 1);
	if (ptr == NULL) {
		error("realloc() failed");
		return 0;
	}

	mem->message = ptr;
	memcpy(&(mem->message[mem->size]), contents, realsize);
	mem->size += realsize;
	mem->message[mem->size] = 0;

	return realsize;
}

extern carbon_record_t* get_intensity(char *url, size_t *record_count)
{
	CURL *curl;
	CURLcode res;
	struct http_response chunk;

	if (curl_global_init(CURL_GLOBAL_ALL) != 0) {
		error("%s: curl_global_init: %m", plugin_type);
		return NULL;
	} else if ((curl = curl_easy_init()) == NULL) {
		error("%s: curl_easy_init: %m", plugin_type);
		curl_global_cleanup();
		return NULL;
	}

	// Initialize the http_response structure
	chunk.message = malloc(1);
	if (chunk.message == NULL) {
		error("Failed to allocate memory");
		curl_easy_cleanup(curl);
		curl_global_cleanup();
		return NULL;
	}
	chunk.size = 0;
	// set headers
	struct curl_slist *headers = NULL;
	headers = curl_slist_append(headers, "Accept: application/json");

	if (curl_easy_setopt(curl, CURLOPT_URL, url) ||
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers) ||
		curl_easy_setopt(curl, CURLOPT_HEADER, 0) ||
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, _write_callback) ||
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *) &chunk)
		) {
			error("%s: curl_easy_setopt() failed", plugin_type);
			curl_slist_free_all(headers);
			free(chunk.message);
			curl_easy_cleanup(curl);
			curl_global_cleanup();
			return NULL;
		}
	// send http request
	if ((res = curl_easy_perform(curl)) != CURLE_OK) {
		error("failed.");
		curl_slist_free_all(headers);
		free(chunk.message);
		curl_easy_cleanup(curl);
		curl_global_cleanup();
		return NULL;
	}

	// info("%lu bytes retrieved", (unsigned long)chunk.size);
	// handle data
	struct json_object *parsed_json = json_tokener_parse(chunk.message);
	if (parsed_json == NULL) {
		error("Wrong Json data.");
		info("Response %s", chunk.message);
		curl_slist_free_all(headers);
		free(chunk.message);
		curl_easy_cleanup(curl);
		curl_global_cleanup();
		return NULL;
	}

	struct json_object *data;
	struct json_object *data_array;
	struct json_object *data_entry;
	struct json_object *from;
	struct json_object *to;
	struct json_object *intensity;
	struct json_object *forecast;

	size_t n_data;
	size_t i;

	carbon_record_t *records = malloc(MAX_RECORDS * sizeof(carbon_record_t));
	if (!records) {
		error("Unable to allocate memory for records");
		json_object_put(parsed_json);
		curl_slist_free_all(headers);
		free(chunk.message);
		curl_easy_cleanup(curl);
		curl_global_cleanup();
		return NULL;
	}

	json_object_object_get_ex(parsed_json, "data", &data);
	json_object_object_get_ex(data, "data", &data_array);
	if (json_object_get_type(data_array) != json_type_array) {
		error("'data' is not an array.");
		// print data to debug
		const char *data_str = json_object_to_json_string(data);
	error("Data: %s", data_str);

		return NULL;
	} else {
		n_data = json_object_array_length(data_array);
	}
	*record_count = (n_data < MAX_RECORDS) ? n_data : MAX_RECORDS;

	for (i = 0; i < *record_count; i++) {
		data_entry = json_object_array_get_idx(data_array, i);

		json_object_object_get_ex(data_entry, "from", &from);
		json_object_object_get_ex(data_entry, "to", &to);
		json_object_object_get_ex(data_entry, "intensity", &intensity);
		json_object_object_get_ex(intensity, "forecast", &forecast);

		strncpy(records[i].from, json_object_get_string(from), sizeof(records[i].from) - 1);
		strncpy(records[i].to, json_object_get_string(to), sizeof(records[i].to) - 1);
		records[i].intensity = json_object_get_int(forecast);
	}

	json_object_put(parsed_json);
	// Clean up
	curl_slist_free_all(headers);
	free(chunk.message);
	curl_easy_cleanup(curl);
	curl_global_cleanup();

	return records;
}

extern double compute_SCI(job_desc_msg_t *job_desc, uint32_t carbon_intensity)
{

	// SCI = (operational carbon + embodied carbon) / walltime
	uint32_t walltime =job_desc->time_limit; // mins

	// Energy = Number of Nodes * Power of the node * Walltime
	uint32_t num_nodes = job_desc->num_tasks / job_desc->ntasks_per_node;

	uint16_t cpus_per_task = job_desc->cpus_per_task == (uint16_t)-1? 1: job_desc->cpus_per_task;
	// Power of the node = Power of the CPU + Power of the GPUs + Power of DRAM + Power of any SSDs/HDDs
	uint32_t power_cpus = ((cpus_per_task * job_desc->ntasks_per_node) / total_cpus) * power_CPU; // w
	uint32_t power_gpus = 0;
	int gpus_per_node = 0;
	if (job_desc->tres_per_node != NULL) {
		gpus_per_node = regex_gpus(job_desc->tres_per_node);
		power_gpus = power_GPU * gpus_per_node;
	}

	uint32_t power_nodes = power_cpus + power_gpus;

	uint32_t energy = num_nodes * power_nodes * walltime; // w*min

	// Opertional Emissions = Energy * Region-specific carbon intensity
	double op_emissions = ((double)energy / 1000 / 60 ) * carbon_intensity; // g

	// Embodied Emissions = Total embodied emissions * Time share * Resource share
	double time_share = (double)walltime / total_time;
	uint16_t ntasks_per_core = job_desc->ntasks_per_core == (uint16_t)-1? 1: job_desc->ntasks_per_core;
	uint16_t cores = job_desc->ntasks_per_node*cpus_per_task/ntasks_per_core;
	double resource_share = (double)cores / total_cpus;
	double em_emissions = embodied_CPU * time_share * resource_share + embodied_GPU * gpus_per_node * time_share;

	double sci = (op_emissions + em_emissions); // / walltime; // g/min

	return sci;
}

/*****************************************************************************\
 * We've provided a simple example of the type of things you can do with this
 * plugin. If you develop another plugin that may be of interest to others
 * please post it to slurm-dev@schedmd.com  Thanks!
\*****************************************************************************/

extern int job_submit(job_desc_msg_t *job_desc, uint32_t submit_uid,
		      char **err_msg)
{
	/* Log select fields from a job submit request. See slurm/slurm.h
	 * for information about additional fields in job_desc_msg_t.
	 * Note that default values for most numbers is NO_VAL */
	info("Job submit request: account:%s begin_time:%ld dependency:%s "
	     "name:%s partition:%s qos:%s submit_uid:%u time_limit:%u "
	     "user_id:%u",
	     job_desc->account, (long)job_desc->begin_time,
	     job_desc->dependency,
	     job_desc->name, job_desc->partition, job_desc->qos,
	     submit_uid, job_desc->time_limit, job_desc->user_id);

	// doc: https://carbon-intensity.github.io/api-definitions/#get-regional-intensity-from-fw24h
	char from[25];
	char url[256];
	size_t record_count;

	get_iso8601_time(from, sizeof(from));
	snprintf(url, sizeof(url),
	 		"https://api.carbonintensity.org.uk/regional/intensity/%s/fw24h/regionid/%d",
	 		from, REGION_ID);
	carbon_record_t* carbin_intensity_matrix = get_intensity(url, &record_count);
	if (carbin_intensity_matrix == NULL) {
		error("Failed to parse carbon intensity data");
	} else {
		// get sci
		double current_sci = 0.0;
		double predicted_sci = 0.0;
		for (size_t i = 0; i < record_count; i++) {
			double sci = compute_SCI(job_desc, carbin_intensity_matrix[i].intensity);
			carbin_intensity_matrix[i].sci = sci;

			if (i == 0) {
				// get the index=0 as the current sci.
				current_sci = sci;
			} else if (i == 1) {
				// get the index=1 as the predicted sci
				predicted_sci = sci;
			}
		}

		double change_rate = 0.0;
		change_rate = (predicted_sci - current_sci) / current_sci;
		if (change_rate > 0) {
			job_desc->change_rate = change_rate + current_sci / MAX_SCI_VALUE;
		} else {
			job_desc->change_rate = change_rate - current_sci / MAX_SCI_VALUE;
		}

		job_desc->carbon_records = carbin_intensity_matrix;
		job_desc->record_count = record_count;
		info("Job %s: current sci is %f, predicted sci is %f, priority change rate is %f",
				job_desc->name, current_sci, predicted_sci, job_desc->change_rate);
	}

	return SLURM_SUCCESS;
}

extern int job_modify(job_desc_msg_t *job_desc, job_record_t *job_ptr,
		      uint32_t submit_uid, char **err_msg)
{
	/* Log select fields from a job modify request. See slurm/slurm.h
	 * for information about additional fields in job_desc_msg_t.
	 * Note that default values for most numbers is NO_VAL */
	info("Job modify request: account:%s begin_time:%ld dependency:%s "
	     "job_id:%u name:%s partition:%s qos:%s submit_uid:%u "
	     "time_limit:%u",
	     job_desc->account, (long)job_desc->begin_time,
	     job_desc->dependency,
	     job_desc->job_id, job_desc->name, job_desc->partition,
	     job_desc->qos, submit_uid, job_desc->time_limit);

	return SLURM_SUCCESS;
}
