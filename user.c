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
	free(u);
}


user *
load_user(crontab_fd, pw, name)
	int		crontab_fd;
	struct passwd	*pw;		/* NULL implies syscrontab */
	char		*name;
{
	char	envstr[MAX_ENVSTR];
	FILE	*file;
	user	*u;
	entry	*e;
	int	status;
	char	**envp, **tenvp;

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
	if ((u->name = strdup(name)) == NULL) {
		free(u);
		errno = ENOMEM;
		return NULL;
	}
	u->crontab = NULL;

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
			err_user = name;
			e = load_entry(file, crontab_error, pw, envp);
			err_user = NULL;
#else
			e = load_entry(file, NULL, pw, envp);
#endif
			if (e) {
				e->next = u->crontab;
				u->crontab = e;
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
