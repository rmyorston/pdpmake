/*
 * Do the actual making for make
 */
#include "make.h"

struct name *target;

void
remove_target(void)
{
	if (!dryrun && !print && !precious &&
			target && !(target->n_flag & (N_PRECIOUS | N_PHONY)) &&
			unlink(target->n_name) == 0) {
		diagnostic("'%s' removed", target->n_name);
	}
}

/*
 * Do commands to make a target
 */
static int
docmds(struct name *np, struct cmd *cp)
{
	int estat = 0;
	char *q, *command;

	for (; cp; cp = cp->c_next) {
		uint8_t ssilent, signore, sdomake;

		// Location of command in makefile (for use in error messages)
		makefile = cp->c_makefile;
		dispno = cp->c_dispno;
#if ENABLE_FEATURE_MAKE_POSIX_202X
		opts &= ~OPT_make;	// We want to know if $(MAKE) is expanded
#endif
		q = command = expand_macros(cp->c_cmd, FALSE);
		ssilent = silent || (np->n_flag & N_SILENT) || dotouch;
		signore = ignore || (np->n_flag & N_IGNORE);
		sdomake = (!dryrun || doinclude || domake) && !dotouch;
		for (;;) {
			if (*q == '@')	// Specific silent
				ssilent = TRUE + 1;
			else if (*q == '-')	// Specific ignore
				signore = TRUE;
			else if (*q == '+')	// Specific domake
				sdomake = TRUE + 1;
			else
				break;
			q++;
		}

		if (sdomake > TRUE) {
			// '+' must not override '@' or .SILENT
			if (ssilent != TRUE + 1 && !(np->n_flag & N_SILENT))
				ssilent = FALSE;
		} else if (!sdomake)
			ssilent = dotouch;

		if (!ssilent) {
			puts(q);
			fflush(stdout);
		}

		if (sdomake) {
			// Get the shell to execute it
			int status;
			char *cmd = !signore ? xconcat3("set -e;", q, "") : q;

			target = np;
			status = system(cmd);
			// If this command was being run to create an include file
			// or bring it up-to-date errors should be ignored and a
			// failure status returned.
			if (status == -1 && !doinclude) {
				error("couldn't execute '%s'", q);
			} else if (status != 0 && !signore) {
#if ENABLE_FEATURE_MAKE_EXTENSIONS
				if (!posix && WIFSIGNALED(status))
					remove_target();
#endif
				if (errcont) {
					diagnostic("failed to build '%s'", np->n_name);
					estat |= MAKE_FAILURE;
					free(command);
					free(cmd);
					break;
				} else if (doinclude) {
					warning("failed to build '%s'", np->n_name);
				} else {
					const char *err_type = NULL;
					int err_value;

					if (WIFEXITED(status)) {
						err_type = "exit";
						err_value = WEXITSTATUS(status);
					} else if (WIFSIGNALED(status)) {
						err_type = "signal";
						err_value = WTERMSIG(status);
					}

					if (err_type)
						error("failed to build '%s' %s %d", np->n_name,
								err_type, err_value);
					else
						error("failed to build '%s'", np->n_name);
				}
			}
			target = NULL;
			if (!signore)
				free(cmd);
		}
		if (sdomake || dryrun || dotouch)
			estat = MAKE_DIDSOMETHING;
		free(command);
	}
	makefile = NULL;
	return estat;
}

/*
 * Update the modification time of a file to now.
 */
static void
touch(struct name *np)
{
	if (dryrun || !silent)
		printf("touch %s\n", np->n_name);

	if (!dryrun) {
		const struct timespec timebuf[2] = {{0, UTIME_NOW}, {0, UTIME_NOW}};

		if (utimensat(AT_FDCWD, np->n_name, timebuf, 0) < 0) {
			if (errno == ENOENT) {
				int fd = open(np->n_name, O_RDWR | O_CREAT, 0666);
				if (fd >= 0) {
					close(fd);
					return;
				}
			}
			warning("touch %s failed: %s\n", np->n_name, strerror(errno));
		}
	}
}

#if !ENABLE_FEATURE_MAKE_POSIX_202X
# define make1(n, c, o, a, d, i) make1(n, c, o, i)
#endif
static int
make1(struct name *np, struct cmd *cp, char *oodate, char *allsrc,
		char *dedup, struct name *implicit)
{
	int estat;
	char *name, *member = NULL, *base;

	name = splitlib(np->n_name, &member);
	setmacro("?", oodate, 0 | M_VALID);
#if ENABLE_FEATURE_MAKE_POSIX_202X
	if (!POSIX_2017) {
		setmacro("+", allsrc, 0 | M_VALID);
		setmacro("^", dedup, 0 | M_VALID);
	}
#endif
	setmacro("%", member, 0 | M_VALID);
	setmacro("@", name, 0 | M_VALID);
	if (implicit) {
		setmacro("<", implicit->n_name, 0 | M_VALID);
		base = member ? member : name;
		*suffix(base) = '\0';
		setmacro("*", base, 0 | M_VALID);
	}
	free(name);

	estat = docmds(np, cp);
	if (dotouch && !(np->n_flag & N_PHONY))
		touch(np);

	return estat;
}

/*
 * Determine if the modification time of a target, t, is less than
 * that of a prerequisite, p.  If the tv_nsec member of either is
 * exactly 0 we assume (possibly incorrectly) that the time resolution
 * is 1 second and only compare tv_sec values.
 */
static int
timespec_le(const struct timespec *t, const struct timespec *p)
{
	if (t->tv_nsec == 0 || p->tv_nsec == 0)
		return t->tv_sec <= p->tv_sec;
	else if (t->tv_sec < p->tv_sec)
		return TRUE;
	else if (t->tv_sec == p->tv_sec)
		return t->tv_nsec <= p->tv_nsec;
	return FALSE;
}

/*
 * Return the greater of two struct timespecs
 */
static const struct timespec *
timespec_max(const struct timespec *t, const struct timespec *p)
{
	return timespec_le(t, p) ? p : t;
}

/*
 * Recursive routine to make a target.
 */
int
make(struct name *np, int level)
{
	struct depend *dp;
	struct rule *rp;
	struct name *impdep = NULL;	// implicit prerequisite
	struct rule imprule;
	struct cmd *sc_cmd = NULL;	// commands for single-colon rule
	char *oodate = NULL;
#if ENABLE_FEATURE_MAKE_POSIX_202X
	char *allsrc = NULL;
	char *dedup = NULL;
#endif
	struct timespec dtim = {1, 0};
	int estat = 0;

	if (np->n_flag & N_DONE)
		return 0;
	if (np->n_flag & N_DOING)
		error("circular dependency for %s", np->n_name);
	np->n_flag |= N_DOING;

	if (!np->n_tim.tv_sec)
		modtime(np);		// Get modtime of this file

	if (!(np->n_flag & N_DOUBLE)) {
		// Find the commands needed for a single-colon rule, using
		// an inference rule or .DEFAULT rule if necessary
		sc_cmd = getcmd(np);
		if (!sc_cmd) {
			impdep = dyndep(np, &imprule);
			if (impdep) {
				sc_cmd = imprule.r_cmd;
				addrule(np, imprule.r_dep, NULL, FALSE);
			}
		}

		// As a last resort check for a default rule
		if (!(np->n_flag & N_TARGET) && np->n_tim.tv_sec == 0) {
			sc_cmd = getcmd(findname(".DEFAULT"));
			if (!sc_cmd) {
				if (doinclude)
					return 1;
				error("don't know how to make %s", np->n_name);
			}
			impdep = np;
		}
	}
#if ENABLE_FEATURE_MAKE_EXTENSIONS
	else {
		// If any double-colon rule has no commands we need
		// an inference rule
		for (rp = np->n_rule; rp; rp = rp->r_next) {
			if (!rp->r_cmd) {
				impdep = dyndep(np, &imprule);
				if (!impdep) {
					if (doinclude)
						return 1;
					error("don't know how to make %s", np->n_name);
				}
				break;
			}
		}
	}
#endif

#if ENABLE_FEATURE_MAKE_EXTENSIONS || ENABLE_FEATURE_MAKE_POSIX_202X
	// Reset flag to detect duplicate prerequisites
	if (!quest && !(np->n_flag & N_DOUBLE)) {
		for (rp = np->n_rule; rp; rp = rp->r_next) {
			for (dp = rp->r_dep; dp; dp = dp->d_next) {
				dp->d_name->n_flag &= ~N_MARK;
			}
		}
	}
#endif

	for (rp = np->n_rule; rp; rp = rp->r_next) {
#if ENABLE_FEATURE_MAKE_EXTENSIONS
		struct name *locdep = NULL;

		// Each double-colon rule is handled separately.
		if ((np->n_flag & N_DOUBLE)) {
			// If the rule has no commands use the inference rule.
			if (!rp->r_cmd) {
				locdep = impdep;
				imprule.r_dep->d_next = rp->r_dep;
				rp->r_dep = imprule.r_dep;
				rp->r_cmd = imprule.r_cmd;
			}
			// A rule with no prerequisities is executed unconditionally.
			if (!rp->r_dep)
				dtim = np->n_tim;
			// Reset flag to detect duplicate prerequisites
			if (!quest) {
				for (dp = rp->r_dep; dp; dp = dp->d_next) {
					dp->d_name->n_flag &= ~N_MARK;
				}
			}
		}
#endif
		for (dp = rp->r_dep; dp; dp = dp->d_next) {
			// Make prerequisite
			estat |= make(dp->d_name, level + 1);

			// Make strings of out-of-date prerequisites (for $?),
			// all prerequisites (for $+) and deduplicated prerequisites
			// (for $^).  But not if we were invoked with -q.
			if (!quest) {
				if (timespec_le(&np->n_tim, &dp->d_name->n_tim)) {
#if ENABLE_FEATURE_MAKE_EXTENSIONS
					if (posix || !(dp->d_name->n_flag & N_MARK))
#endif
						oodate = xappendword(oodate, dp->d_name->n_name);
				}
#if ENABLE_FEATURE_MAKE_POSIX_202X
				allsrc = xappendword(allsrc, dp->d_name->n_name);
				if (!(dp->d_name->n_flag & N_MARK))
					dedup = xappendword(dedup, dp->d_name->n_name);
#endif
#if ENABLE_FEATURE_MAKE_EXTENSIONS || ENABLE_FEATURE_MAKE_POSIX_202X
				dp->d_name->n_flag |= N_MARK;
#endif
			}
			dtim = *timespec_max(&dtim, &dp->d_name->n_tim);
		}
#if ENABLE_FEATURE_MAKE_EXTENSIONS
		if ((np->n_flag & N_DOUBLE)) {
			if (!quest && ((np->n_flag & N_PHONY) ||
							timespec_le(&np->n_tim, &dtim))) {
				if (!(estat & MAKE_FAILURE)) {
					estat |= make1(np, rp->r_cmd, oodate, allsrc,
										dedup, locdep);
					dtim = (struct timespec){1, 0};
				}
				free(oodate);
				oodate = NULL;
			}
#if ENABLE_FEATURE_MAKE_POSIX_202X
			free(allsrc);
			free(dedup);
			allsrc = dedup = NULL;
#endif
			if (locdep) {
				rp->r_dep = rp->r_dep->d_next;
				rp->r_cmd = NULL;
			}
		}
#endif
	}
#if ENABLE_FEATURE_MAKE_EXTENSIONS
	if ((np->n_flag & N_DOUBLE) && impdep)
		free(imprule.r_dep);
#endif

	np->n_flag |= N_DONE;
	np->n_flag &= ~N_DOING;

	if (quest) {
		if (timespec_le(&np->n_tim, &dtim)) {
			// MAKE_FAILURE means rebuild is needed
			estat = MAKE_FAILURE | MAKE_DIDSOMETHING;
		}
	} else if (!(np->n_flag & N_DOUBLE) &&
				((np->n_flag & N_PHONY) || (timespec_le(&np->n_tim, &dtim)))) {
		if (!(estat & MAKE_FAILURE)) {
			if (sc_cmd)
				estat |= make1(np, sc_cmd, oodate, allsrc, dedup, impdep);
			else if (!doinclude && level == 0 && !(estat & MAKE_DIDSOMETHING))
				warning("nothing to be done for %s", np->n_name);
		} else if (!doinclude) {
			diagnostic("'%s' not built due to errors", np->n_name);
		}
		free(oodate);
	}

	if (estat & MAKE_DIDSOMETHING)
		clock_gettime(CLOCK_REALTIME, &np->n_tim);
	else if (!quest && level == 0 && !timespec_le(&np->n_tim, &dtim))
		printf("%s: '%s' is up to date\n", myname, np->n_name);

#if ENABLE_FEATURE_MAKE_POSIX_202X
	free(allsrc);
	free(dedup);
#endif
	return estat;
}
