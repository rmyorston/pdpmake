/*
 *	Do the actual making for make
 */

#include <stdio.h>
#include <sys/stat.h>
#include <sys/err.h>
#include "h.h"



/*
 *	Exec a shell that returns exit status correctly (/bin/esh).
 *	The standard EON shell returns the process number of the last
 *	async command, used by the debugger (ugg).
 *	[exec on eon is like a fork+exec on unix]
 */
int
dosh(string, shell)
char *			string;
char *			shell;
{
	int	number;

	return ((number = execl(shell, shell,"-c", string, 0)) == -1) ?
		-1:	/* couldn't start the shell */
		wait(number);	/* return its exit status */
}


/*
 *	Do commands to make a target
 */
void
docmds(np)
struct name *		np;
{
	bool			ssilent = silent;
	bool			signore = ignore;
	int			estat;
	register char *		q;
	register char *		p;
	char *			shell;
	register struct line *	lp;
	register struct cmd *	cp;


	if (*(shell = getmacro("SHELL")) == '\0')
		shell = ":bin/esh";

	for (lp = np->n_line; lp; lp = lp->l_next)
		for (cp = lp->l_cmd; cp; cp = cp->c_next)
		{
			strcpy(str1, cp->c_cmd);
			expand(str1);
			q = str1;
			while ((*q == '@') || (*q == '-'))
			{
				if (*q == '@')	   /*  Specific silent  */
					ssilent = TRUE;
				else		   /*  Specific ignore  */
					signore = TRUE;
				q++;		   /*  Not part of the command  */
			}

			if (!ssilent)
				fputs("    ", stdout);

			for (p=q; *p; p++)
			{
				if (*p == '\n' && p[1] != '\0')
				{
					*p = ' ';
					if (!ssilent)
						fputs("\\\n", stdout);
				}
				else if (!ssilent)
					putchar(*p);
			}
			if (!ssilent)
				putchar('\n');

			if (domake)
			{			/*  Get the shell to execute it  */
				if ((estat = dosh(q, shell)) != 0)
				{
					if (estat == -1)
						fatal("Couldn't execute %s", shell);
					else
					{
						printf("%s: Error code %d", myname, estat);
						if (signore)
							fputs(" (Ignored)\n", stdout);
						else
						{
							putchar('\n');
							if (!(np->n_flag & N_PREC))
								if (unlink(np->n_name) == 0)
									printf("%s: '%s' removed.\n", myname, np->n_name);
							exit(estat);
						}
					}
				}
			}
		}
}


/*
 *	Get the modification time of a file.  If the first
 *	doesn't exist, it's modtime is set to 0.
 */
void
modtime(np)
struct name *		np;
{
	struct stat		info;
	int			fd;


	if ((fd = open(np->n_name, 0)) < 0)
	{
		if (errno != ER_NOTF)
			fatal("Can't open %s; error %02x", np->n_name, errno);

		np->n_time = 0L;
	}
	else if (getstat(fd, &info) < 0)
		fatal("Can't getstat %s; error %02x", np->n_name, errno);
	else
		np->n_time = info.st_mod;

	close(fd);
}


/*
 *	Update the mod time of a file to now.
 */
void
touch(np)
struct name *		np;
{
	char			c;
	int			fd;


	if (!domake || !silent)
		printf("    touch %s\n", np->n_name);

	if (domake)
	{
		if ((fd = open(np->n_name, 0)) < 0)
			printf("%s: '%s' no touched - non-existant\n",
					myname, np->n_name);
		else
		{
			uread(fd, &c, 1, 0);
			uwrite(fd, &c, 1);
		}
		close(fd);
	}
}


/*
 *	Recursive routine to make a target.
 */
int
make(np, level)
struct name *		np;
int			level;
{
	register struct depend *	dp;
	register struct line *		lp;
	time_t				dtime = 1;


	if (np->n_flag & N_DONE)
		return 0;

	if (!np->n_time)
		modtime(np);		/*  Gets modtime of this file  */

	if (rules)
	{
		for (lp = np->n_line; lp; lp = lp->l_next)
			if (lp->l_cmd)
				break;
		if (!lp)
			dyndep(np);
	}

	if (!(np->n_flag & N_TARG) && np->n_time == 0L)
		fatal("Don't know how to make %s", np->n_name);

	for (lp = np->n_line; lp; lp = lp->l_next)
		for (dp = lp->l_dep; dp; dp = dp->d_next)
		{
			make(dp->d_name, level+1);
			dtime = max(dtime, dp->d_name->n_time);
		}

	np->n_flag |= N_DONE;

	if (quest)
	{
		rtime(&np->n_time);
		return np->n_time < dtime;
	}
	else if (np->n_time < dtime)
	{
		if (dotouch)
			touch(np);
		else
		{
			setmacro("@", np->n_name);
			docmds(np);
		}
		rtime(&np->n_time);
	}
	else if (level == 0)
		printf("%s: '%s' is up to date\n", myname, np->n_name);
	return 0;
}
