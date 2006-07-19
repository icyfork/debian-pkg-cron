/* Copyright 1988,1990,1993,1994 by Paul Vixie
 * All rights reserved
 *
 * Distribute freely, except: don't remove my name from the source or
 * documentation (don't take credit for my work), mark your changes (don't
 * get me blamed for your possible bugs), don't alter or remove this
 * notice.  May be sold if buildable source is provided to buyer.  No
 * warrantee of any kind, express or implied, is included with this
 * software; use at your own risk, responsibility for damages (if any) to
 * anyone resulting from the use of this software rests entirely with the
 * user.
 *
 * Send bug reports, bug fixes, enhancements, requests, flames, etc., and
 * I'll try to keep a version up to date.  I can be reached as follows:
 * Paul Vixie          <paul@vix.com>          uunet!decwrl!vixie!paul
 */

#if !defined(lint) && !defined(LINT)
static char rcsid[] = "$Id: user.c,v 2.8 1994/01/15 20:43:43 vixie Exp $";
#endif

/* vix 26jan87 [log is in RCS file]
 */


#include <syslog.h>
#include <string.h>
#include "cron.h"


#ifdef WITH_SELINUX
#include <selinux/selinux.h>
#include <selinux/flask.h>
#include <selinux/av_permissions.h>
#include <selinux/get_context_list.h>

static int get_security_context(char *name, int crontab_fd, security_context_t
                                *rcontext, char *tabname) {
    security_context_t scontext=NULL;
    security_context_t  file_context=NULL;
    struct av_decision avd;
    int retval=0;

    *rcontext = NULL;
    if (get_default_context(name, NULL, &scontext)) {
        if (security_getenforce() > 0) {
            log_it(name, getpid(), "No SELinux security context", tabname);
            return -1;
        } else {
            log_it(name, getpid(),
                   "No security context but SELinux in permissive mode,"
                   " continuing", tabname);
	    return 0;
        }
    }

    if (fgetfilecon(crontab_fd, &file_context) < OK) {
        if (security_getenforce() > 0) {
            log_it(name, getpid(), "getfilecon FAILED", tabname);
            freecon(scontext);
            return -1;
        } else {
            log_it(name, getpid(), "getfilecon FAILED but SELinux in "
                   "permissive mode, continuing", tabname);
            *rcontext=scontext;
            return 0;
        }
    }

    /*
     * Since crontab files are not directly executed,
     * crond must ensure that the crontab file has
     * a context that is appropriate for the context of
     * the user cron job.  It performs an entrypoint
     * permission check for this purpose.
     */

    retval = security_compute_av(scontext,
                                 file_context,
                                 SECCLASS_FILE,
                                 FILE__ENTRYPOINT,
                                 &avd);
    freecon(file_context);
    if (retval || ((FILE__ENTRYPOINT & avd.allowed) != FILE__ENTRYPOINT)) {
        if (security_getenforce() > 0) {
            log_it(name, getpid(), "ENTRYPOINT FAILED", tabname);
            freecon(scontext);
            return -1;
        } else {
            log_it(name, getpid(), "ENTRYPOINT FAILED but SELinux in permissive mode, continuing", tabname);
        }
    }
    *rcontext=scontext;
    return 0;
}
#endif


#ifdef DEBIAN
/* Function used to log errors in crontabs from cron daemon. (User
   crontabs are checked before they're accepted, but system crontabs
   are not. */
static char *err_user=NULL;

void
crontab_error(msg)
     char *msg;
{
  const char *fn;
  /* Figure out the file name from the username */
  if (0 == strcmp(err_user,"*system*")) {
    syslog(LOG_ERR|LOG_CRON,"Error: %s; while reading %s", msg, SYSCRONTAB);
  } else if (0 == strncmp(err_user,"*system*",8)) {
    fn = err_user+8;
    syslog(LOG_ERR|LOG_CRON,"Error: %s; while reading %s/%s", msg, 
	   SYSCRONDIR,fn);
  } else {
    syslog(LOG_ERR|LOG_CRON, "Error: %s; while reading crontab for user %s",
	   msg, err_user);
  }
}

#endif

void
free_user(u)
	user	*u;
{
	entry	*e, *ne;

	free(u->name);
	for (e = u->crontab;  e != NULL;  e = ne) {
		ne = e->next;
		free_entry(e);
	}
#ifdef WITH_SELINUX
	if (u->scontext)
		freecon(u->scontext);
#endif
	free(u);
}


user *
load_user(crontab_fd, pw, uname, fname, tabname)
	int		crontab_fd;
	struct passwd	*pw;		/* NULL implies syscrontab */
	char		*uname;
	char		*fname;
	char		*tabname;
{
	char	envstr[MAX_ENVSTR];
	FILE	*file;
	user	*u;
	entry	*e;
	int	status;
	char	**envp = NULL, **tenvp;

	if (!(file = fdopen(crontab_fd, "r"))) {
		perror("fdopen on crontab_fd in load_user");
		return NULL;
	}

	Debug(DPARS, ("load_user()\n"))

	/* file is open.  build user entry, then read the crontab file.
	 */
	if ((u = (user *) malloc(sizeof(user))) == NULL) {
		errno = ENOMEM;
		return NULL;
	}
	if ((u->name = strdup(fname)) == NULL) {
		free(u);
		errno = ENOMEM;
		return NULL;
	}
	u->crontab = NULL;

#ifdef WITH_SELINUX
	u->scontext = NULL;
        if (is_selinux_enabled() > 0) {
            char *sname=uname;
            if (pw==NULL) {
                sname="system_u";
            }
            if (get_security_context(sname, crontab_fd, 
                                     &u->scontext, tabname) != 0 ) {
		u->scontext = NULL;
                free_user(u);
                u = NULL;
                goto done;
            }
        }
#endif


	/* 
	 * init environment.  this will be copied/augmented for each entry.
	 */
	if ((envp = env_init()) == NULL) {
		free(u->name);
		free(u);
		return NULL;
	}

	/*
	 * load the crontab
	 */
	while ((status = load_env(envstr, file)) >= OK) {
		switch (status) {
		case ERR:
			free_user(u);
			u = NULL;
			goto done;
		case FALSE:
#ifdef DEBIAN
			err_user = fname;
			e = load_entry(file, crontab_error, pw, envp);
			err_user = NULL;
#else
			e = load_entry(file, NULL, pw, envp);
#endif
			if (e) {
				e->next = u->crontab;
				u->crontab = e;
			} else {
				/* stop processing on EOF/syntax error */
				free_user(u);
				u = NULL;
				goto done;
			}
			break;
		case TRUE:
			if ((tenvp = env_set(envp, envstr))) {
				envp = tenvp;
			} else {
				free_user(u);
				u = NULL;
				goto done;
			}
			break;
		}
	}

 done:
	env_free(envp);
	fclose(file);
	Debug(DPARS, ("...load_user() done\n"))
	return u;
}
