/*
 *	Control of the implicit suffix rules
 */


#include "h.h"


/*
 *	Return a pointer to the suffix of a name
 */
char *
suffix(name)
char *			name;
{
	return rindex(name, '.');
}


/*
 *	Dynamic dependency.  This routine applies the suffis rules
 *	to try and find a source and a set of rules for a missing
 *	target.  If found, np is made into a target with the implicit
 *	source name, and rules.  Returns TRUE if np was made into
 *	a target.
 */
bool
dyndep(np)
struct name *		np;
{
	register char *		p;
	register char *		q;
	register char *		suff;		/*  Old suffix  */
	register char *		basename;	/*  Name without suffix  */
	struct name *		op;		/*  New dependent  */
	struct name *		sp;		/*  Suffix  */
	struct line *		lp;
	struct depend *		dp;
	char *			newsuff;


	p = str1;
	q = np->n_name;
	suff = suffix(q);
	while (q < suff)
		*p++ = *q++;
	*p = '\0';
	basename = setmacro("*", str1)->m_val;

	if (!((sp = newname(".SUFFIXES"))->n_flag & N_TARG))
		return FALSE;

	for (lp = sp->n_line; lp; lp = lp->l_next)
		for (dp = lp->l_dep; dp; dp = dp->d_next)
		{
			newsuff = dp->d_name->n_name;
			if (strlen(suff)+strlen(newsuff)+1 >= LZ)
				fatal("Suffix rule too long");
			p = str1;
			q = newsuff;
			while (*p++ = *q++)
				;
			p--;
			q = suff;
			while (*p++ = *q++)
				;
			sp = newname(str1);
			if (sp->n_flag & N_TARG)
			{
				p = str1;
				q = basename;
				if (strlen(basename) + strlen(newsuff)+1 >= LZ)
					fatal("Implicit name too long");
				while (*p++ = *q++)
					;
				p--;
				q = newsuff;
				while (*p++ = *q++)
					;
				op = newname(str1);
				if (!op->n_time)
					modtime(op);
				if (op->n_time)
				{
					dp = newdep(op, 0);
					newline(np, dp, sp->n_line->l_cmd);
					setmacro("<", op->n_name);
					return TRUE;
				}
			}
		}
	return FALSE;
}


/*
 *	Make the default rules
 */
void
makerules()
{
	struct cmd *		cp;
	struct name *		np;
	struct depend *		dp;


	setmacro("BDSCC", "asm");
	/*	setmacro("BDSCFLAGS", "");	*/
	cp = newcmd("$(BDSCC) $(BDSCFLAGS) -n $<", 0);
	np = newname(".c.o");
	newline(np, 0, cp);

	setmacro("CC", "c");
	setmacro("CFLAGS", "-O");
	cp = newcmd("$(CC) $(CFLAGS) -c $<", 0);
	np = newname(".c.obj");
	newline(np, 0, cp);

	setmacro("M80", "asm -n");
	/*	setmacro("M80FLAGS", "");	*/
	cp = newcmd("$(M80) $(M80FLAGS) $<", 0);
	np = newname(".mac.o");
	newline(np, 0, cp);

	setmacro("AS", "zas");
	/*	setmacro("ASFLAGS", "");	*/
	cp = newcmd("$(ZAS) $(ASFLAGS) -o $@ $<", 0);
	np = newname(".as.obj");
	newline(np, 0, cp);

	np = newname(".as");
	dp = newdep(np, 0);
	np = newname(".obj");
	dp = newdep(np, dp);
	np = newname(".c");
	dp = newdep(np, dp);
	np = newname(".o");
	dp = newdep(np, dp);
	np = newname(".mac");
	dp = newdep(np, dp);
	np = newname(".SUFFIXES");
	newline(np, dp, 0);
}
