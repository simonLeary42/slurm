/*****************************************************************************\
 *  jobcomp.c - implementation-independent job completion logging functions
 *****************************************************************************
 *  Copyright (C) 2003 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Jay Windley <jwindley@lnxi.com>, Morris Jette <jette1@llnl.com>
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

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "src/common/macros.h"
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/interfaces/jobcomp.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xassert.h"
#include "src/common/xstring.h"
#include "src/slurmctld/slurmctld.h"

typedef struct slurm_jobcomp_ops {
	int (*set_loc)(void);
	int (*record_job_end)(job_record_t *job_ptr, uint32_t event);
	list_t *(*get_jobs)(slurmdb_job_cond_t *params);
	int (*record_job_start)(job_record_t *job_ptr, uint32_t event);
} slurm_jobcomp_ops_t;

/*
 * These strings must be kept in the same order as the fields
 * declared for slurm_jobcomp_ops_t.
 */
static const char *syms[] = {
	"jobcomp_p_set_location",
	"jobcomp_p_record_job_end",
	"jobcomp_p_get_jobs",
	"jobcomp_p_record_job_start",
};

static slurm_jobcomp_ops_t ops;
static plugin_context_t *g_context = NULL;
static pthread_mutex_t context_lock = PTHREAD_MUTEX_INITIALIZER;
static plugin_init_t plugin_inited = PLUGIN_NOT_INITED;

extern void
jobcomp_destroy_job(void *object)
{
	jobcomp_job_rec_t *job = (jobcomp_job_rec_t *)object;
	if (job) {
		xfree(job->partition);
		xfree(job->start_time);
		xfree(job->end_time);
		xfree(job->uid_name);
		xfree(job->gid_name);
		xfree(job->nodelist);
		xfree(job->jobname);
		xfree(job->state);
		xfree(job->timelimit);
		xfree(job->blockid);
		xfree(job->connection);
		xfree(job->reboot);
		xfree(job->rotate);
		xfree(job->geo);
		xfree(job->bg_start_point);
		xfree(job->work_dir);
		xfree(job->resv_name);
		xfree(job->tres_fmt_req_str);
		xfree(job->account);
		xfree(job->qos_name);
		xfree(job->wckey);
		xfree(job->cluster);
		xfree(job->submit_time);
		xfree(job->eligible_time);
		xfree(job->exit_code);
		xfree(job->derived_ec);
		xfree(job);
	}
}


extern int jobcomp_g_init(void)
{
	int retval = SLURM_SUCCESS;
	char *plugin_type = "jobcomp";

	slurm_mutex_lock(&context_lock);

	if (plugin_inited)
		goto done;

	if (!slurm_conf.job_comp_type) {
		plugin_inited = PLUGIN_NOOP;
		goto done;
	}

	g_context = plugin_context_create(plugin_type,
					  slurm_conf.job_comp_type,
					  (void **) &ops, syms, sizeof(syms));

	if (!g_context) {
		error("cannot create %s context for %s",
		      plugin_type, slurm_conf.job_comp_type);
		retval = SLURM_ERROR;
		plugin_inited = PLUGIN_NOT_INITED;
		goto done;
	}
	plugin_inited = PLUGIN_INITED;
done:
	if (g_context)
		retval = (*(ops.set_loc))();
	slurm_mutex_unlock(&context_lock);
	return retval;
}

extern int jobcomp_g_fini(void)
{
	slurm_mutex_lock(&context_lock);

	if (!g_context)
		goto done;

	plugin_context_destroy(g_context);
	g_context = NULL;

done:
	plugin_inited = PLUGIN_NOT_INITED;
	slurm_mutex_unlock(&context_lock);
	return SLURM_SUCCESS;
}

extern int jobcomp_g_record_job_end(job_record_t *job_ptr)
{
	int retval = SLURM_SUCCESS;

	xassert(plugin_inited != PLUGIN_NOT_INITED);

	if (plugin_inited == PLUGIN_NOOP)
		return SLURM_SUCCESS;

	slurm_mutex_lock(&context_lock);

	xassert(g_context);
	retval = (*(ops.record_job_end))(job_ptr, JOBCOMP_EVENT_JOB_FINISH);

	slurm_mutex_unlock(&context_lock);
	return retval;
}

extern list_t *jobcomp_g_get_jobs(slurmdb_job_cond_t *job_cond)
{
	list_t *job_list = NULL;

	xassert(plugin_inited != PLUGIN_NOT_INITED);

	if (plugin_inited == PLUGIN_NOOP)
		return NULL;

	slurm_mutex_lock(&context_lock);
	xassert(g_context);
	job_list = (*(ops.get_jobs))(job_cond);
	slurm_mutex_unlock(&context_lock);
	return job_list;
}

extern int jobcomp_g_set_location(void)
{
	int retval = SLURM_SUCCESS;

	xassert(plugin_inited != PLUGIN_NOT_INITED);

	if (plugin_inited == PLUGIN_NOOP)
		return SLURM_SUCCESS;

	slurm_mutex_lock(&context_lock);
	xassert(g_context);
	retval = (*(ops.set_loc))();
	slurm_mutex_unlock(&context_lock);
	return retval;
}

extern int jobcomp_g_record_job_start(job_record_t *job_ptr)
{
	int retval = SLURM_SUCCESS;

	xassert(plugin_inited != PLUGIN_NOT_INITED);

	if (plugin_inited == PLUGIN_NOOP)
		return SLURM_SUCCESS;

	slurm_mutex_lock(&context_lock);

	xassert(g_context);
	retval = (*(ops.record_job_start))(job_ptr, JOBCOMP_EVENT_JOB_START);

	slurm_mutex_unlock(&context_lock);
	return retval;
}
