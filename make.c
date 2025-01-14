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

/*
 * Do commands to make a target
 */
static int
docmds(struct name *np, struct cmd *cp)
{
	int estat = 0;
	char *q, *command;

	for (; cp; cp = cp->c_next) {
		uint32_t ssilent, signore, sdomake;

		// Location of command in makefile (for use in error messages)
		makefile = cp->c_makefile;
		dispno = cp->c_dispno;
#if ENABLE_FEATURE_MAKE_POSIX_2024
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
			do {
				q++;
			} while (isblank(*q));
		}

		if (sdomake > TRUE) {
			// '+' must not override '@' or .SILENT
			if (ssilent != TRUE + 1 && !(np->n_flag & N_SILENT))
				ssilent = FALSE;
		} else if (!sdomake)
			ssilent = dotouch;

		if (!ssilent && *q != '\0') {	// Ignore empty commands
			puts(q);
			fflush(stdout);
		}

		if (quest && sdomake != TRUE + 1) {
			// MAKE_FAILURE means rebuild is needed
			estat |= MAKE_FAILURE | MAKE_DIDSOMETHING;
			continue;
		}

		if (sdomake && *q != '\0') {	// Ignore empty commands
			// Get the shell to execute it
			int status;
			char *cmd = !signore IF_FEATURE_MAKE_EXTENSIONS(&& posix) ?
							xconcat3("set -e;", q, "") : q;

			target = np;
			status = system(cmd);
			if (!signore IF_FEATURE_MAKE_EXTENSIONS(&& posix))
				free(cmd);
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
				if (doinclude) {
					warning("failed to build '%s'", np->n_name);
				} else {
					const char *err_type = NULL;
					int err_value = 1;

					if (WIFEXITED(status)) {
						err_type = "exit";
						err_value = WEXITSTATUS(status);
					} else if (WIFSIGNALED(status)) {
						err_type = "signal";
						err_value = WTERMSIG(status);
					}

					if (!quest || err_value == 127) {
						if (err_type)
							diagnostic("failed to build '%s' %s %d",
									np->n_name, err_type, err_value);
						else
							diagnostic("failed to build '%s'", np->n_name);
					}

					if (errcont) {
						estat |= MAKE_FAILURE;
						free(command);
						break;
					}
					exit(2);
				}
			}
			target = NULL;
		}
		if (sdomake || dryrun)
			estat = MAKE_DIDSOMETHING;
		free(command);
	}

	if (dotouch && !(np->n_flag & N_PHONY) && !(estat & MAKE_DIDSOMETHING)) {
		touch(np);
		estat = MAKE_DIDSOMETHING;
	}

	makefile = NULL;
	return estat;
}

#if !ENABLE_FEATURE_MAKE_POSIX_2024 && !ENABLE_FEATURE_MAKE_EXTENSIONS
# define make1(n, c, o, a, d, i, t) make1(n, c, o, i)
#elif ENABLE_FEATURE_MAKE_POSIX_2024 && !ENABLE_FEATURE_MAKE_EXTENSIONS
# define make1(n, c, o, a, d, i, t) make1(n, c, a, d, o, i)
#elif !ENABLE_FEATURE_MAKE_POSIX_2024 && ENABLE_FEATURE_MAKE_EXTENSIONS
# define make1(n, c, o, a, d, i, t) make1(n, c, o, i, t)
#endif
static int
make1(struct name *np, struct cmd *cp, char *oodate, char *allsrc,
		char *dedup, struct name *implicit, const char *tsuff)
{
	char *name, *member = NULL, *base = NULL, *prereq = NULL;

	name = splitlib(np->n_name, &member);
	setmacro("?", oodate, 0 | M_VALID);
#if ENABLE_FEATURE_MAKE_POSIX_2024
	if (!POSIX_2017) {
		setmacro("+", allsrc, 0 | M_VALID);
		setmacro("^", dedup, 0 | M_VALID);
	}
#endif
	setmacro("%", member, 0 | M_VALID);
	setmacro("@", name, 0 | M_VALID);
	if (implicit IF_FEATURE_MAKE_EXTENSIONS(|| !posix)) {
		char *s;

#if ENABLE_FEATURE_MAKE_EXTENSIONS
		// As an extension, if we're not dealing with an implicit
		// prerequisite set $< to the first out-of-date prerequisite.
		if (implicit == NULL) {
			if (oodate) {
				s = strchr(oodate, ' ');
				if (s)
					*s = '\0';
				prereq = oodate;
			}
		} else
#endif
			prereq = implicit->n_name;

#if ENABLE_FEATURE_MAKE_EXTENSIONS
		if (!posix && member == NULL) {
			// As an extension remove the suffix from a target, either
			// that obtained by an inference rule or one of the known
			// suffixes.  Not for targets of the form lib.a(member.o).
			if (tsuff != NULL) {
				base = has_suffix(name, tsuff);
				if (base) {
					free(name);
					name = base;
				}
			} else {
				struct name *xp = newname(".SUFFIXES");
				for (struct rule *rp = xp->n_rule; rp; rp = rp->r_next) {
					for (struct depend *dp = rp->r_dep; dp; dp = dp->d_next) {
						base = has_suffix(name, dp->d_name->n_name);
						if (base) {
							free(name);
							name = base;
							goto done;
						}
					}
				}
			}
		} else
#endif
		{
			base = member ? member : name;
			s = suffix(base);
#if ENABLE_FEATURE_MAKE_EXTENSIONS
			// As an extension, if we're not dealing with an implicit
			// prerequisite and the target ends with a known suffix,
			// remove it and set $* to the stem, else to an empty string.
			if (implicit == NULL && !is_suffix(s))
				base = NULL;
			else
#endif
				*s = '\0';
		}
	}
#if ENABLE_FEATURE_MAKE_EXTENSIONS
 done:
#endif
	setmacro("<", prereq, 0 | M_VALID);
	setmacro("*", base, 0 | M_VALID);
	free(name);

	return docmds(np, cp);
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
	struct rule infrule;
	struct cmd *sc_cmd = NULL;	// commands for single-colon rule
	char *oodate = NULL;
#if ENABLE_FEATURE_MAKE_POSIX_2024
	char *allsrc = NULL;
	char *dedup = NULL;
#endif
#if ENABLE_FEATURE_MAKE_EXTENSIONS
	const char *tsuff = NULL;
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
		// an inference rule or .DEFAULT rule if necessary (but,
		// as an extension, not for phony targets)
		sc_cmd = getcmd(np);
		if (!sc_cmd
#if ENABLE_FEATURE_MAKE_EXTENSIONS && ENABLE_FEATURE_MAKE_POSIX_2024
				&& (posix || !(np->n_flag & N_PHONY))
#endif
				) {
			impdep = dyndep(np, &infrule, &tsuff);
			if (impdep) {
				sc_cmd = infrule.r_cmd;
				addrule(np, infrule.r_dep, NULL, FALSE);
			}
		}

		// As a last resort check for a default rule
		if (!(np->n_flag & N_TARGET) && np->n_tim.tv_sec == 0) {
#if ENABLE_FEATURE_MAKE_EXTENSIONS && ENABLE_FEATURE_MAKE_POSIX_2024
			if (posix || !(np->n_flag & N_PHONY))
#endif
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
		// an inference rule (but, as an extension, not for phony targets)
		for (rp = np->n_rule; rp; rp = rp->r_next) {
			if (!rp->r_cmd) {
# if ENABLE_FEATURE_MAKE_POSIX_2024
				if (posix || !(np->n_flag & N_PHONY))
# endif
					impdep = dyndep(np, &infrule, &tsuff);
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

#if ENABLE_FEATURE_MAKE_EXTENSIONS || ENABLE_FEATURE_MAKE_POSIX_2024
	// Reset flag to detect duplicate prerequisites
	if (!(np->n_flag & N_DOUBLE)) {
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
				infrule.r_dep->d_next = rp->r_dep;
				rp->r_dep = infrule.r_dep;
				rp->r_cmd = infrule.r_cmd;
			}
			// A rule with no prerequisities is executed unconditionally.
			if (!rp->r_dep)
				dtim = np->n_tim;
			// Reset flag to detect duplicate prerequisites
			for (dp = rp->r_dep; dp; dp = dp->d_next) {
				dp->d_name->n_flag &= ~N_MARK;
			}
		}
#endif
		for (dp = rp->r_dep; dp; dp = dp->d_next) {
			// Make prerequisite
			estat |= make(dp->d_name, level + 1);

			// Make strings of out-of-date prerequisites (for $?),
			// all prerequisites (for $+) and deduplicated prerequisites
			// (for $^).
			if (timespec_le(&np->n_tim, &dp->d_name->n_tim)) {
#if ENABLE_FEATURE_MAKE_EXTENSIONS
				if (posix || !(dp->d_name->n_flag & N_MARK))
#endif
					oodate = xappendword(oodate, dp->d_name->n_name);
			}
#if ENABLE_FEATURE_MAKE_POSIX_2024
			allsrc = xappendword(allsrc, dp->d_name->n_name);
			if (!(dp->d_name->n_flag & N_MARK))
				dedup = xappendword(dedup, dp->d_name->n_name);
#endif
#if ENABLE_FEATURE_MAKE_EXTENSIONS || ENABLE_FEATURE_MAKE_POSIX_2024
			dp->d_name->n_flag |= N_MARK;
#endif
			dtim = *timespec_max(&dtim, &dp->d_name->n_tim);
		}
#if ENABLE_FEATURE_MAKE_EXTENSIONS
		if ((np->n_flag & N_DOUBLE)) {
			if (((np->n_flag & N_PHONY) || timespec_le(&np->n_tim, &dtim))) {
				if (!(estat & MAKE_FAILURE)) {
					estat |= make1(np, rp->r_cmd, oodate, allsrc,
										dedup, locdep, tsuff);
					dtim = (struct timespec){1, 0};
				}
				free(oodate);
				oodate = NULL;
			}
#if ENABLE_FEATURE_MAKE_POSIX_2024
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
		free(infrule.r_dep);
#endif

	np->n_flag |= N_DONE;
	np->n_flag &= ~N_DOING;

	if (!(np->n_flag & N_DOUBLE) &&
				((np->n_flag & N_PHONY) || (timespec_le(&np->n_tim, &dtim)))) {
		if (!(estat & MAKE_FAILURE)) {
			if (sc_cmd)
				estat |= make1(np, sc_cmd, oodate, allsrc, dedup,
								impdep, tsuff);
			else if (!doinclude && level == 0 && !(estat & MAKE_DIDSOMETHING))
				warning("nothing to be done for %s", np->n_name);
		} else if (!doinclude && !quest) {
			diagnostic("'%s' not built due to errors", np->n_name);
		}
		free(oodate);
	}

	if (estat & MAKE_DIDSOMETHING) {
		modtime(np);
		if (!np->n_tim.tv_sec)
			clock_gettime(CLOCK_REALTIME, &np->n_tim);
	} else if (!quest && level == 0 && !timespec_le(&np->n_tim, &dtim))
		printf("%s: '%s' is up to date\n", myname, np->n_name);

#if ENABLE_FEATURE_MAKE_POSIX_2024
	free(allsrc);
	free(dedup);
#endif
	return estat;
}
