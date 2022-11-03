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
 * target.  NULL is returned on failure.  On success the name of
 * the implicit prerequisite is returned and the details are
 * placed in the imprule structure provided by the caller.
 */
struct name *
dyndep(struct name *np, struct rule *imprule)
{
	char *suff, *newsuff;
	char *base, *name, *member;
	struct name *xp;		// Suffixes
	struct name *sp;		// Suffix rule
	struct name *pp = NULL;	// Implicit prerequisite
	struct rule *rp;
	struct depend *dp;
	IF_NOT_FEATURE_MAKE_EXTENSIONS(const) bool chain = FALSE;

	member = NULL;
	name = splitlib(np->n_name, &member);

	suff = xstrdup(suffix(name));
	base = member ? member : name;
	*suffix(base) = '\0';

	xp = newname(".SUFFIXES");
#if ENABLE_FEATURE_MAKE_EXTENSIONS
 retry:
#endif
	for (rp = xp->n_rule; rp; rp = rp->r_next) {
		for (dp = rp->r_dep; dp; dp = dp->d_next) {
			// Generate new suffix rule to try
			newsuff = dp->d_name->n_name;
			sp = namecat(newsuff, suff, FALSE);
			if (sp && sp->n_rule) {
				// Generate a name for an implicit prerequisite
				struct name *ip = namecat(base, newsuff, TRUE);
				if ((ip->n_flag & N_DOING))
					continue;
				if (!ip->n_tim.tv_sec)
					modtime(ip);
				if (chain ? ip->n_tim.tv_sec || (ip->n_flag & N_TARGET) :
							dyndep(ip, NULL) != NULL) {
					// Prerequisite exists or we know how to make it
					if (imprule) {
						imprule->r_dep = newdep(ip, NULL);
						imprule->r_cmd = sp->n_rule->r_cmd;
					}
					pp = ip;
					goto finish;
				}
			}
		}
	}
#if ENABLE_FEATURE_MAKE_EXTENSIONS
	// If we didn't find an existing file or an explicit rule try
	// again, this time looking for a chained inference rule.
	if (!posix && !chain) {
		chain = TRUE;
		goto retry;
	}
#endif
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
	char *r = s;
	static const char *rulepos = NULL;

	if (rulepos == NULL)
		rulepos = (RULES MACROS) + (norules ? sizeof(RULES) - 1 : 0);

	if (*rulepos == '\0')
		return NULL;

	while (--size) {
		if ((*r++ = *rulepos++) == '\n')
			break;
	}
	*r = '\0';
	return s;
}
