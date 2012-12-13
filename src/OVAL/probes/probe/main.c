/*
 * Copyright 2011 Red Hat Inc., Durham, North Carolina.
 * All Rights Reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, 
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software 
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Authors:
 *      Daniel Kopecek <dkopecek@redhat.com>
 *      Tomas Heinrich <theinric@redhat.com>
 */
#include <config.h>
#if !defined(_GNU_SOURCE)
# if defined(HAVE_PTHREAD_TIMEDJOIN_NP) && defined(HAVE_CLOCK_GETTIME)
#  define _GNU_SOURCE
# endif
#endif
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include <errno.h>
#include <libgen.h>
#include <seap.h>
#include "common/bfind.h"
#include "probe.h"
#include "ncache.h"
#include "rcache.h"
#include "icache.h"
#include "worker.h"
#include "signal_handler.h"
#include "input_handler.h"
#include "probe-api.h"

static int fail(int err, const char *who, int line)
{
	fprintf(stderr, "FAIL: %d:%s: %d, %s\n", line, who, err, strerror(err));
	exit(err);
}

void  *OSCAP_GSYM(probe_arg)          = NULL;
bool   OSCAP_GSYM(varref_handling)    = true;
char **OSCAP_GSYM(no_varref_ents)     = NULL;
size_t OSCAP_GSYM(no_varref_ents_cnt) = 0;
pthread_barrier_t OSCAP_GSYM(th_barrier);

extern probe_ncache_t *OSCAP_GSYM(ncache);

static int probe_optecmp(char **a, char **b)
{
	return strcmp(*a, *b);
}

static SEXP_t *probe_reset(SEXP_t *arg0, void *arg1)
{
        probe_t *probe = (probe_t *)arg1;
        /*
         * FIXME: implement main loop locking & worker waiting
         */
	probe_rcache_free(probe->rcache);
        probe_ncache_free(probe->ncache);

        probe->rcache = probe_rcache_new();
        probe->ncache = probe_ncache_new();

        return(NULL);
}

static int probe_opthandler_varref(int option, va_list args)
{
	bool  o_switch;
	char *o_name;
	char *o_temp;

	o_switch = va_arg(args, int);
	o_name   = va_arg(args, char *);

	if (o_name == NULL) {
		/* switch varref handling on/off globally */
		OSCAP_GSYM(varref_handling) = o_switch;
		return (0);
	}

	o_temp = oscap_bfind (OSCAP_GSYM(no_varref_ents), OSCAP_GSYM(no_varref_ents_cnt),
			      sizeof(char *), o_name, (int(*)(void *, void *)) &probe_optecmp);

	if (o_temp != NULL)
		return (0);

	OSCAP_GSYM(no_varref_ents) = oscap_realloc(OSCAP_GSYM(no_varref_ents),
						   sizeof (char *) * ++OSCAP_GSYM(no_varref_ents_cnt));
	OSCAP_GSYM(no_varref_ents)[OSCAP_GSYM(no_varref_ents_cnt) - 1] = strdup(o_name);

	qsort(OSCAP_GSYM(no_varref_ents), OSCAP_GSYM(no_varref_ents_cnt),
              sizeof (char *), (int(*)(const void *, const void *))&probe_optecmp);

	return (0);
}

static int probe_opthandler_rcache(int option, va_list args)
{
	return (0);
}

int main(int argc, char *argv[])
{
	pthread_attr_t th_attr;
	sigset_t       sigmask;
	probe_t        probe;
	const char *OSCAP_PROBE_RSH;
	char *probe_command, *probe_exec[10];

	OSCAP_PROBE_RSH = getenv("OSCAP_PROBE_RSH");

	if (OSCAP_PROBE_RSH) {
		probe_command = (char *)malloc(strlen(OSCAP_PROBE_RSH) + 1 + strlen(argv[0]) + 1);
		sprintf(probe_command, "%s %s", OSCAP_PROBE_RSH, argv[0]);

		probe_exec[0] = "/bin/sh";
		probe_exec[1] = "-c";
		probe_exec[2] = probe_command;
		probe_exec[3] = NULL;

		execv(probe_exec[0], probe_exec);
		fprintf(stderr, "cann't exec %s: %s\n", probe_exec[0], strerror(errno));
		fail(errno, "cannot exec OSCAP_PROBE_RSH", __LINE__);
	}

	if ((errno = pthread_barrier_init(&OSCAP_GSYM(th_barrier), NULL,
	                                  1 + // signal thread
	                                  1 + // input thread
	                                  1 + // icache thread
	                                  0)) != 0)
	{
		fail(errno, "pthread_barrier_init", __LINE__ - 6);
	}

	/*
	 * Block signals, any signals received will be
	 * handled by the signal handler thread.
	 */
	sigemptyset(&sigmask);
	sigaddset(&sigmask, SIGHUP);
	sigaddset(&sigmask, SIGUSR1);
	sigaddset(&sigmask, SIGUSR2);
	sigaddset(&sigmask, SIGINT);
	sigaddset(&sigmask, SIGTERM);
	sigaddset(&sigmask, SIGQUIT);
        sigaddset(&sigmask, SIGPIPE);

	if (pthread_sigmask(SIG_BLOCK, &sigmask, NULL))
		fail(errno, "pthread_sigmask", __LINE__ - 1);

	probe.flags = 0;
	probe.pid   = getpid();
	probe.name  = basename(argv[0]);
        probe.probe_exitcode = 0;

	/*
	 * Initialize SEAP stuff
	 */
	probe.SEAP_ctx = SEAP_CTX_new();
	probe.sd       = SEAP_openfd2(probe.SEAP_ctx, STDIN_FILENO, STDOUT_FILENO, 0);

	if (probe.sd < 0)
		fail(errno, "SEAP_openfd2", __LINE__ - 3);

	if (SEAP_cmd_register(probe.SEAP_ctx, PROBECMD_RESET, 0, &probe_reset) != 0)
		fail(errno, "SEAP_cmd_register", __LINE__ - 1);

	/*
	 * Initialize result & name caching
	 */
	probe.rcache = probe_rcache_new();
	probe.ncache = probe_ncache_new();
        probe.icache = probe_icache_new();

        OSCAP_GSYM(ncache) = probe.ncache;

	/*
	 * Initialize probe option handlers
	 */
#define PROBE_OPTION_INITCOUNT 2

	probe.option = oscap_alloc(sizeof(probe_option_t) * PROBE_OPTION_INITCOUNT);
	probe.optcnt = PROBE_OPTION_INITCOUNT;

        probe.option[0].option  = PROBE_VARREF_HANDLING;
        probe.option[0].handler = &probe_opthandler_varref;
        probe.option[1].option  = PROBE_RESULT_CACHING;
        probe.option[1].handler = &probe_opthandler_rcache;

	/*
	 * Create signal handler
	 */
	pthread_attr_init(&th_attr);

	if (pthread_attr_setdetachstate(&th_attr, PTHREAD_CREATE_JOINABLE))
		fail(errno, "pthread_attr_setdetachstate", __LINE__ - 1);

	if (pthread_create(&probe.th_signal, &th_attr, &probe_signal_handler, &probe))
		fail(errno, "pthread_create(probe_signal_handler)", __LINE__ - 1);

	pthread_attr_destroy(&th_attr);

	/*
	 * Create input handler (detached)
	 */
        probe.workers   = rbt_i32_new();
        probe.probe_arg = probe_init();

	pthread_attr_init(&th_attr);

	if (pthread_create(&probe.th_input, &th_attr, &probe_input_handler, &probe))
		fail(errno, "pthread_create(probe_input_handler)", __LINE__ - 1);

	pthread_attr_destroy(&th_attr);

	/*
	 * Wait until the signal handler exits
	 */
	if (pthread_join(probe.th_signal, NULL))
		fail(errno, "pthread_join", __LINE__ - 1);

	/*
	 * Wait for the input_handler thread
	 */
#if defined(HAVE_PTHREAD_TIMEDJOIN_NP) && defined(HAVE_CLOCK_GETTIME)
	{
		struct timespec j_tm;

		if (clock_gettime(CLOCK_REALTIME, &j_tm) == -1)
			fail(errno, "clock_gettime", __LINE__ - 1);

		j_tm.tv_sec += 3;

		if (pthread_timedjoin_np(probe.th_input, NULL, &j_tm) != 0)
			fail(errno, "pthread_timedjoin_np", __LINE__ - 1);
	}
#else
	if (pthread_join(probe.th_input, NULL))
		fail(errno, "pthread_join", __LINE__ - 1);
#endif
	/*
	 * Cleanup
	 */
        probe_fini(probe.probe_arg);

	probe_ncache_free(probe.ncache);
	probe_rcache_free(probe.rcache);
        probe_icache_free(probe.icache);

        rbt_i32_free(probe.workers);

        if (probe.sd != -1)
                SEAP_close(probe.SEAP_ctx, probe.sd);

	SEAP_CTX_free(probe.SEAP_ctx);
        oscap_free(probe.option);

	return (probe.probe_exitcode);
}
