/*
 * Check structures for make.
 */
#include "make.h"

static void
print_name(struct name *np)
{
	if (np == firstname)
		printf("# default target\n");
	printf("%s:", np->n_name);
	if ((np->n_flag & N_DOUBLE))
		putchar(':');
}

static void
print_prerequisites(struct rule *rp)
{
	struct depend *dp;

	for (dp = rp->r_dep; dp; dp = dp->d_next)
		printf(" %s", dp->d_name->n_name);
}

static void
print_commands(struct rule *rp)
{
	struct cmd *cp;

	for (cp = rp->r_cmd; cp; cp = cp->c_next)
		printf("\t%s\n", cp->c_cmd);
}

void
print_details(void)
{
	struct macro *mp;
	struct name *np;
	struct rule *rp;

	for (mp = macrohead; mp; mp = mp->m_next)
		printf("%s = %s\n", mp->m_name, mp->m_val);
	putchar('\n');

	for (np = namehead; np; np = np->n_next) {
		if (!(np->n_flag & N_DOUBLE)) {
			print_name(np);
			for (rp = np->n_rule; rp; rp = rp->r_next) {
				print_prerequisites(rp);
			}
			putchar('\n');

			for (rp = np->n_rule; rp; rp = rp->r_next) {
				print_commands(rp);
			}
			putchar('\n');
		} else {
			for (rp = np->n_rule; rp; rp = rp->r_next) {
				print_name(np);
				print_prerequisites(rp);
				putchar('\n');

				print_commands(rp);
				putchar('\n');
			}
		}
	}
}
