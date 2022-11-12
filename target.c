/*
 * Process name, rule, command and prerequisite structures
 */
#include "make.h"

/*
 * Add a prerequisite to the end of the supplied list.
 * Return the new head pointer for that list.
 */
struct depend *
newdep(struct name *np, struct depend *dphead)
{
	struct depend *dpnew;
	struct depend *dp;

	dpnew = xmalloc(sizeof(struct depend));
	dpnew->d_next = NULL;
	dpnew->d_name = np;
	dpnew->d_refcnt = 0;

	if (dphead == NULL)
		return dpnew;

	for (dp = dphead; dp->d_next; dp = dp->d_next)
		;

	dp->d_next = dpnew;

	return dphead;
}

void
freedeps(struct depend *dp)
{
	struct depend *nextdp;

	if (dp && --dp->d_refcnt <= 0) {
		for (; dp; dp = nextdp) {
			nextdp = dp->d_next;
			free(dp);
		}
	}
}

/*
 * Add a command to the end of the supplied list of commands.
 * Return the new head pointer for that list.
 */
struct cmd *
newcmd(char *str, struct cmd *cphead)
{
	struct cmd *cpnew;
	struct cmd *cp;

	while (isspace(*str))
		str++;

	if (*str == '\0')		// No command, return current head
		return cphead;

	cpnew = xmalloc(sizeof(struct cmd));
	cpnew->c_next = NULL;
	cpnew->c_cmd = xstrdup(str);
	cpnew->c_refcnt = 0;

	if (cphead == NULL)
		return cpnew;

	for (cp = cphead; cp->c_next; cp = cp->c_next)
		;

	cp->c_next = cpnew;

	return cphead;
}

void
freecmds(struct cmd *cp)
{
	struct cmd *nextcp;

	if (cp && --cp->c_refcnt <= 0) {
		for (; cp; cp = nextcp) {
			nextcp = cp->c_next;
			free(cp->c_cmd);
			free(cp);
		}
	}
}

struct name *namehead[HTABSIZE];
struct name *firstname;

struct name *
findname(const char *name)
{
	struct name *np;

	for (np = namehead[getbucket(name)]; np; np = np->n_next) {
		if (strcmp(name, np->n_name) == 0)
			return np;
	}
	return NULL;
}

static int
is_valid_target(const char *name)
{
	const char *s;
	for (s = name; *s; ++s) {
		if (IF_FEATURE_MAKE_EXTENSIONS(posix &&)
				((ENABLE_FEATURE_MAKE_POSIX_202X && !POSIX_2017)?
						!(isfname(*s) || *s == '/') : !ispname(*s)))
			return FALSE;
	}
	return TRUE;
}

/*
 * Intern a name.  Return a pointer to the name struct
 */
struct name *
newname(const char *name)
{
	struct name *np = findname(name);

	if (np == NULL) {
		unsigned int bucket;

		if (!is_valid_target(name))
			error("invalid target name '%s'", name);

		bucket = getbucket(name);
		np = xmalloc(sizeof(struct name));
		np->n_next = namehead[bucket];
		namehead[bucket] = np;
		np->n_name = xstrdup(name);
		np->n_rule = NULL;
		np->n_tim = (struct timespec){0, 0};
		np->n_flag = 0;
	}
	return np;
}

/*
 * Return the commands on the first rule that has them or NULL.
 */
struct cmd *
getcmd(struct name *np)
{
	struct rule *rp;

	if (np == NULL)
		return NULL;

	for (rp = np->n_rule; rp; rp = rp->r_next)
		if (rp->r_cmd)
			return rp->r_cmd;
	return NULL;
}

#if ENABLE_FEATURE_CLEAN_UP
void
freenames(void)
{
	int i;
	struct name *np, *nextnp;

	for (i = 0; i < HTABSIZE; i++) {
		for (np = namehead[i]; np; np = nextnp) {
			nextnp = np->n_next;
			free(np->n_name);
			freerules(np->n_rule);
			free(np);
		}
	}
}
#endif

void
freerules(struct rule *rp)
{
	struct rule *nextrp;

	for (; rp; rp = nextrp) {
		nextrp = rp->r_next;
		freedeps(rp->r_dep);
		freecmds(rp->r_cmd);
		free(rp);
	}
}

static void *
inc_ref(void *vp)
{
	if (vp) {
		struct depend *dp = vp;
		if (dp->d_refcnt == INT_MAX)
			error("out of memory");
		dp->d_refcnt++;
	}
	return vp;
}

/*
 * Add a new rule to a target.  This checks to see if commands already
 * exist for the target.  If flag is TRUE the target can have multiple
 * rules with commands (double-colon rules).
 *
 * i)  If the name is a special target and there are no prerequisites
 *     or commands to be added remove all prerequisites and commands.
 *     This is necessary when clearing a built-in inference rule.
 * ii) If name is a special target and has commands, replace them.
 *     This is for redefining commands for an inference rule.
 */
void
addrule(struct name *np, struct depend *dp, struct cmd *cp, int flag)
{
	struct rule *rp;
	struct rule **rpp;

#if ENABLE_FEATURE_MAKE_EXTENSIONS
	// Can't mix single-colon and double-colon rules
	if (!posix && (np->n_flag & N_TARGET)) {
		if (!(np->n_flag & N_DOUBLE) != !flag)		// like xor
			error("inconsistent rules for target %s", np->n_name);
	}
#endif

	// Clear out prerequisites and commands
	if ((np->n_flag & N_SPECIAL) && !dp && !cp) {
#if ENABLE_FEATURE_MAKE_POSIX_202X
		if (strcmp(np->n_name, ".PHONY") == 0)
			return;
#endif
		freerules(np->n_rule);
		np->n_rule = NULL;
		return;
	}

	if (cp && !(np->n_flag & N_DOUBLE) && getcmd(np)) {
		// Handle the inference rule redefinition case
		if ((np->n_flag & N_SPECIAL) && !dp) {
			freerules(np->n_rule);
			np->n_rule = NULL;
		} else {
			error("commands defined twice for target %s", np->n_name);
		}
	}

	rpp = &np->n_rule;
	while (*rpp)
		rpp = &(*rpp)->r_next;

	*rpp = rp = xmalloc(sizeof(struct rule));
	rp->r_next = NULL;
	rp->r_dep = inc_ref(dp);
	rp->r_cmd = inc_ref(cp);

	np->n_flag |= N_TARGET;
	if (flag)
		np->n_flag |= N_DOUBLE;
}
