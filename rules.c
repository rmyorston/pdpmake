/*
 * Control of the implicit suffix rules
 */
#include "make.h"

/*
 * Return a pointer to the suffix of a name (which may be the
 * terminating NUL if there's no suffix).
 */
char *
suffix(const char *name)
{
	char *p = strrchr(name, '.');
	return p ? p : (char *)name + strlen(name);
}

/*
 * Find a name structure whose name is formed by concatenating two
 * strings.  If 'create' is TRUE the name is created if necessary.
 */
static struct name *
namecat(const char *s, const char *t, int create)
{
	char *p;
	struct name *np;

	p = xconcat3(s, t, "");
	np = create ? newname(p) : findname(p);
	free(p);
	return np;
}

/*
 * Dynamic dependency.  This routine applies the suffix rules
 * to try and find a source and a set of rules for a missing
 * target.  If found, np is made into a target with the implicit
 * source name and rules.  For single colon rules this is done by
 * adding a new rule; double colon rules are temporarily modified
 * in-place.
 */
struct name *
dyndep(struct name *np, struct rule *implicit)
{
	char *suff, *newsuff;
	char *base, *name, *member;
	struct name *xp;		// Suffixes
	struct name *sp;		// Suffix rule
	struct name *pp = NULL;	// Implicit prerequisite
	struct rule *rp, *rp2;
	struct depend *dp;
	bool hascmds = FALSE;

	member = NULL;
	name = splitlib(np->n_name, &member);

	suff = xstrdup(suffix(name));
	base = member ? member : name;
	*suffix(base) = '\0';

	xp = newname(".SUFFIXES");
	for (rp = xp->n_rule; rp; rp = rp->r_next) {
		for (dp = rp->r_dep; dp; dp = dp->d_next) {
			// Generate new suffix rule to try
			newsuff = dp->d_name->n_name;
			sp = namecat(newsuff, suff, FALSE);
			if (sp && sp->n_rule) {
				// Generate a name for an implicit prerequisite
				pp = namecat(base, newsuff, TRUE);
				if (!pp->n_time)
					modtime(pp);
				if (!pp->n_time) {
					// File doesn't currently exist, can we make it?
					for (rp2 = pp->n_rule; rp2; rp2 = rp2->r_next) {
						if (rp2->r_cmd) {
							hascmds = TRUE;
							break;
						}
					}
				}
				if (pp->n_time || hascmds) {
					dp = newdep(pp, NULL);
					if (implicit) {
						dp->d_next = implicit->r_dep;
						implicit->r_dep = dp;
						implicit->r_cmd = sp->n_rule->r_cmd;
					} else {
						addrule(np, dp, sp->n_rule->r_cmd, FALSE);
					}
					goto finish;
				}
				pp = NULL;
			}
		}
	}
 finish:
	free(suff);
	free(name);
	return pp;
}

#define RULES \
	".SUFFIXES:.o .c .y .l .a .sh .f\n" \
	".c.o:\n" \
	"	$(CC) $(CFLAGS) -c $<\n" \
	".f.o:\n" \
	"	$(FC) $(FFLAGS) -c $<\n" \
	".y.o:\n" \
	"	$(YACC) $(YFLAGS) $<\n" \
	"	$(CC) $(CFLAGS) -c y.tab.c\n" \
	"	rm -f y.tab.c\n" \
	"	mv y.tab.o $@\n" \
	".y.c:\n" \
	"	$(YACC) $(YFLAGS) $<\n" \
	"	mv y.tab.c $@\n" \
	".l.o:\n" \
	"	$(LEX) $(LFLAGS) $<\n" \
	"	$(CC) $(CFLAGS) -c lex.yy.c\n" \
	"	rm -f lex.yy.c\n" \
	"	mv lex.yy.o $@\n" \
	".l.c:\n" \
	"	$(LEX) $(LFLAGS) $<\n" \
	"	mv lex.yy.c $@\n" \
	".c.a:\n" \
	"	$(CC) -c $(CFLAGS) $<\n" \
	"	$(AR) $(ARFLAGS) $@ $*.o\n" \
	"	rm -f $*.o\n" \
	".f.a:\n" \
	"	$(FC) -c $(FFLAGS) $<\n" \
	"	$(AR) $(ARFLAGS) $@ $*.o\n" \
	"	rm -f $*.o\n" \
	".c:\n" \
	"	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<\n" \
	".f:\n" \
	"	$(FC) $(FFLAGS) $(LDFLAGS) -o $@ $<\n" \
	".sh:\n" \
	"	cp $< $@\n" \
	"	chmod a+x $@\n"

#define MACROS \
	"MAKE=make\n" \
	"CC=c99\n" \
	"CFLAGS=-O1\n" \
	"FC=fort77\n" \
	"FFLAGS=-O1\n" \
	"YACC=yacc\n" \
	"YFLAGS=\n" \
	"LEX=lex\n" \
	"LFLAGS=\n" \
	"AR=ar\n" \
	"ARFLAGS=-rv\n" \
	"LDFLAGS=\n"

/*
 * Read the built-in rules using a fake fgets-like interface.
 */
char *
getrules(char *s, int size)
{
	char *r;
	static const char *pos = NULL;

	if (pos == NULL)
		pos = (RULES MACROS) + (norules ? sizeof(RULES) - 1 : 0);

	if (*pos == '\0')
		return NULL;

	r = s;
	while (--size) {
		*s++ = *pos;
		if (*pos++ == '\n')
			break;
	}
	*s = '\0';
	return r;
}
