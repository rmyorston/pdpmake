/*
 * make [--posix] [-C path] [-f makefile] [-j num] [-x pragma]
 *      [-ehiknpqrsSt] [macro[::[:]]=val ...] [target ...]
 *
 *  --posix  Enforce POSIX mode (non-POSIX)
 *  -C  Change directory to path (non-POSIX)
 *  -f  Makefile name
 *  -j  Number of jobs to run in parallel (not implemented)
 *  -x  Pragma to make POSIX mode less strict (non-POSIX)
 *  -e  Environment variables override macros in makefiles
 *  -h  Display help information (non-POSIX)
 *  -i  Ignore exit status
 *  -k  Continue on error
 *  -n  Pretend to make
 *  -p  Print all macros & targets
 *  -q  Question up-to-dateness of target.  Return exit status 1 if not
 *  -r  Don't use inbuilt rules
 *  -s  Make silently
 *  -S  Stop on error
 *  -t  Touch files instead of making them
 */
#include "make.h"

uint32_t opts;
const char *myname;
const char *makefile;
struct file *makefiles;
bool posix;
bool seen_first;
#if ENABLE_FEATURE_MAKE_POSIX_202X
char *numjobs = NULL;
#endif
#if ENABLE_FEATURE_MAKE_EXTENSIONS
unsigned char pragma = 0;
unsigned char posix_level = DEFAULT_POSIX_LEVEL;
#endif

static void
usage(int exit_code)
{
	FILE *fp = ENABLE_FEATURE_MAKE_EXTENSIONS && exit_code == 0 ?
				stdout : stderr;

	fprintf(fp,
		"Usage: %s"
		IF_FEATURE_MAKE_EXTENSIONS(" [--posix] [-C path]")
		" [-f makefile]"
		IF_FEATURE_MAKE_POSIX_202X(" [-j num]")
		IF_FEATURE_MAKE_EXTENSIONS(" [-x pragma]")
		IF_FEATURE_MAKE_EXTENSIONS("\n\t")
		IF_NOT_FEATURE_MAKE_EXTENSIONS(" [-eiknpqrsSt] ")
		IF_FEATURE_MAKE_EXTENSIONS(" [-ehiknpqrsSt] ")
		IF_NOT_FEATURE_MAKE_POSIX_202X("[macro=val ...]")
		IF_FEATURE_MAKE_POSIX_202X("[macro[::[:]]=val ...]")
		" [target ...]\n", myname);

	fprintf(fp, "\nThis build supports:"
			IF_FEATURE_MAKE_EXTENSIONS(" non-POSIX extensions,")
			IF_FEATURE_MAKE_POSIX_202X(" POSIX 202X,")
			" POSIX 2017\n");
#if ENABLE_FEATURE_MAKE_EXTENSIONS && ENABLE_FEATURE_MAKE_POSIX_202X
	fprintf(fp,
			"In strict POSIX mode the %s standard is enforced by default.\n",
			DEFAULT_POSIX_LEVEL == STD_POSIX_2017 ? "2017" : "202X");
#endif
#if !ENABLE_FEATURE_MAKE_EXTENSIONS && !ENABLE_FEATURE_MAKE_POSIX_202X
	fprintf(fp, "\nFor details see:\n"
	"  https://pubs.opengroup.org/onlinepubs/9699919799/utilities.2018edition/make.html\n");
#endif
	exit(exit_code);
}

/*
 * Process options from an argv array.  If from_env is non-zero we're
 * handling options from MAKEFLAGS so skip '-C', '-f', '-p' and '-x'.
 */
static uint32_t
process_options(int argc, char **argv, int from_env)
{
	int opt;
	uint32_t flags = 0;

	while ((opt = getopt(argc, argv, OPTSTR1 OPTSTR2)) != -1) {
		switch(opt) {
#if ENABLE_FEATURE_MAKE_EXTENSIONS
		case 'C':
			if (!posix && !from_env) {
				if (chdir(optarg) == -1) {
					error("can't chdir to %s: %s", optarg, strerror(errno));
				}
				flags |= OPT_C;
				break;
			}
			error("-C not allowed");
			break;
#endif
		case 'f':	// Alternate file name
			if (!from_env) {
				makefiles = newfile(optarg, makefiles);
				flags |= OPT_f;
			}
			break;
		case 'e':	// Prefer env vars to macros in makefiles
			flags |= OPT_e;
			break;
		case 'h':	// Print usage message and exit
			usage(0);
			break;
		case 'i':	// Ignore fault mode
			flags |= OPT_i;
			break;
#if ENABLE_FEATURE_MAKE_POSIX_202X
		case 'j':
			if (!POSIX_2017) {
				const char *s;

				for (s = optarg; *s; ++s) {
					if (!isdigit(*s)) {
						usage(2);
					}
				}
				free(numjobs);
				numjobs = xstrdup(optarg);
				flags |= OPT_j;
				break;
			}
			error("-j not allowed");
			break;
#endif
		case 'k':	// Continue on error
			flags |= OPT_k;
			flags &= ~OPT_S;
			break;
		case 'n':	// Pretend mode
			flags |= OPT_n;
			break;
		case 'p':
			if (!from_env)
				flags |= OPT_p;
			break;
		case 'q':
			flags |= OPT_q;
			break;
		case 'r':
			flags |= OPT_r;
			break;
		case 't':
			flags |= OPT_t;
			break;
		case 's':	// Silent about commands
			flags |= OPT_s;
			break;
		case 'S':	// Stop on error
			flags |= OPT_S;
			flags &= ~OPT_k;
			break;
#if ENABLE_FEATURE_MAKE_EXTENSIONS
		case 'x':	// Pragma
			if (!from_env) {
				set_pragma(optarg);
				flags |= OPT_x;
			}
			break;
#endif
		default:
			if (from_env)
				error("invalid MAKEFLAGS");
			else
				usage(2);
		}
	}
	return flags;
}

/*
 * Split the contents of MAKEFLAGS into an argv array.  If the return
 * value (call it fargv) isn't NULL the caller should free fargv[1] and
 * fargv.
 */
static char **
expand_makeflags(int *fargc)
{
	const char *m, *makeflags = getenv("MAKEFLAGS");
	char *p, *argstr;
	int argc;
	char **argv;

	if (makeflags == NULL)
		return NULL;

	while (isblank(*makeflags))
		makeflags++;

	if (*makeflags == '\0')
		return NULL;

	p = argstr = xmalloc(strlen(makeflags) + 2);

	// If MAKEFLAGS doesn't start with a hyphen, doesn't look like
	// a macro definition and only contains valid option characters,
	// add a hyphen.
	argc = 3;
	if (makeflags[0] != '-' && strchr(makeflags, '=') == NULL) {
		if (strspn(makeflags, OPTSTR1) != strlen(makeflags))
			error("invalid MAKEFLAGS");
		*p++ = '-';
	} else {
		// MAKEFLAGS may need to be split, estimate size of argv array.
		for (m = makeflags; *m; ++m) {
			if (isblank(*m))
				argc++;
		}
	}

	argv = xmalloc(argc * sizeof(char *));
	argc = 0;
	argv[argc++] = (char *)myname;
	argv[argc++] = argstr;

	// Copy MAKEFLAGS into argstr, splitting at non-escaped blanks.
	m = makeflags;
	do {
		if (*m == '\\' && m[1] != '\0')
			m++;	// Skip backslash, copy next character unconditionally.
		else if (isblank(*m)) {
			// Terminate current argument and start a new one.
			*p++ = '\0';
			argv[argc++] = p;
			do {
				m++;
			} while (isblank(*m));
			continue;
		}
		*p++ = *m++;
	} while (*m != '\0');
	*p = '\0';
	argv[argc] = NULL;

	*fargc = argc;
	return argv;
}

/*
 * Instantiate all macros in an argv-style array of pointers.  Stop
 * processing at the first string that doesn't contain an equal sign.
 * As an extension, target arguments on the command line (level 1)
 * are skipped and will be processed later.
 */
static char **
process_macros(char **argv, int level)
{
	char *equal;

	for (; *argv; argv++) {
#if ENABLE_FEATURE_MAKE_POSIX_202X
		char *colon = NULL;
		int immediate = 0;
		int except_dollar = FALSE;
#endif

		if (!(equal = strchr(*argv, '='))) {
#if ENABLE_FEATURE_MAKE_EXTENSIONS
			// Skip targets on the command line
			if (!posix && level == 1)
				continue;
			else
#endif
				// Stop at first target
				break;
		}

#if ENABLE_FEATURE_MAKE_POSIX_202X
		if (equal - 2 > *argv && equal[-1] == ':' && equal[-2] == ':') {
			if (POSIX_2017)
				error("invalid macro assignment");
			if (equal - 3 > *argv  && equal[-3] == ':') {
				// BSD-style ':='.  Expand RHS, but not '$$',
				// resulting macro is delayed expansion.
				colon = equal - 3;
				except_dollar = TRUE;
			} else {
				// GNU-style ':='. Expand RHS, including '$$',
				// resulting macro is immediate expansion.
				colon = equal - 2;
				immediate = M_IMMEDIATE;
			}
			*colon = '\0';
		} else
#endif
			*equal = '\0';

		/* We want to process _most_ macro assignments.
		 * There are exceptions for particular values from the
		 * environment (level 3). */
		if (!(level == 3 &&
				(strcmp(*argv, "MAKEFLAGS") == 0
					|| strcmp(*argv, "SHELL") == 0
#if ENABLE_FEATURE_MAKE_POSIX_202X
					|| (strcmp(*argv, "CURDIR") == 0 && !useenv && !POSIX_2017)
#endif

				))) {
#if ENABLE_FEATURE_MAKE_POSIX_202X
			if (colon) {
				char *exp = expand_macros(equal + 1, except_dollar);
				setmacro(*argv, exp, level | immediate);
				free(exp);
			} else
#endif
				setmacro(*argv, equal + 1, level);
		}

#if ENABLE_FEATURE_MAKE_POSIX_202X
		if (colon)
			*colon = ':';
		else
#endif
			*equal = '=';
	}
	return argv;
}

/*
 * Update the MAKEFLAGS macro and environment variable to include any
 * command line options that don't have their default value (apart from
 * -f, -p and -S).  Also add any macros defined on the command line or
 * by the MAKEFLAGS environment variable (apart from MAKEFLAGS itself).
 * Add macros that were defined on the command line to the environment.
 */
static void
update_makeflags(void)
{
	int i;
	char optbuf[] = "-?";
	char *makeflags = NULL;
	char *macro, *s;
	const char *t;
	struct macro *mp;

	t = OPTSTR1;
	for (i = 0; *t; t++) {
#if ENABLE_FEATURE_MAKE_POSIX_202X
		if (*t == ':')
			continue;
#endif
		if ((opts & OPT_MASK & (1 << i))) {
			optbuf[1] = *t;
			makeflags = xappendword(makeflags, optbuf);
#if ENABLE_FEATURE_MAKE_POSIX_202X
			if (*t == 'j') {
				makeflags = xappendword(makeflags, numjobs);
			}
#endif
		}
		i++;
	}

	for (i = 0; i < HTABSIZE; ++i) {
		for (mp = macrohead[i]; mp; mp = mp->m_next) {
			if ((mp->m_level == 1 || mp->m_level == 2) &&
					strcmp(mp->m_name, "MAKEFLAGS") != 0) {
				macro = xmalloc(strlen(mp->m_name) + 2 * strlen(mp->m_val) + 1);
				s = stpcpy(macro, mp->m_name);
				*s++ = '=';
				for (t = mp->m_val; *t; t++) {
					if (*t == '\\' || isblank(*t))
						*s++ = '\\';
					*s++ = *t;
				}
				*s = '\0';

				makeflags = xappendword(makeflags, macro);
				free(macro);

				// Add command line macro definitions to the environment
				if (mp->m_level == 1 && strcmp(mp->m_name, "SHELL") != 0)
					setenv(mp->m_name, mp->m_val, 1);
			}
		}
	}

	if (makeflags) {
		setmacro("MAKEFLAGS", makeflags, 0);
		setenv("MAKEFLAGS", makeflags, 1);
		free(makeflags);
	}
}

static void
make_handler(int sig)
{
	signal(sig, SIG_DFL);
	remove_target();
	kill(getpid(), sig);
}

static void
init_signal(int sig)
{
	struct sigaction sa, new_action;

	sigemptyset(&new_action.sa_mask);
	new_action.sa_flags = 0;
	new_action.sa_handler = make_handler;

	sigaction(sig, NULL, &sa);
	if (sa.sa_handler != SIG_IGN)
		sigaction(sig, &new_action, NULL);
}

/*
 * If the global option flag associated with a special target hasn't
 * been set mark all prerequisites of the target with a flag.  If the
 * target had no prerequisites set the global option flag.
 */
static void
mark_special(const char *special, uint32_t oflag, uint16_t nflag)
{
	struct name *np;
	struct rule *rp;
	struct depend *dp;
	int marked = FALSE;

	if (!(opts & oflag) && (np = findname(special))) {
		for (rp = np->n_rule; rp; rp = rp->r_next) {
			for (dp = rp->r_dep; dp; dp = dp->d_next) {
				dp->d_name->n_flag |= nflag;
				marked = TRUE;
			}
		}

		if (!marked)
			opts |= oflag;
	}
}

int
main(int argc, char **argv)
{
#if ENABLE_FEATURE_MAKE_POSIX_202X
	const char *path, *newpath = NULL;
#else
	const char *path = "make";
#endif
	char **fargv, **fargv0;
	int fargc, estat;
	bool found_target;
	FILE *ifd;
	struct file *fp;

	if (argc == 0) {
		return EXIT_FAILURE;
	}

	myname = basename(*argv);
#if ENABLE_FEATURE_MAKE_EXTENSIONS
	if (argv[1] && strcmp(argv[1], "--posix") == 0) {
		argv[1] = argv[0];
		++argv;
		--argc;
		setenv("PDPMAKE_POSIXLY_CORRECT", "", 1);
		posix = TRUE;
	} else {
		posix = getenv("PDPMAKE_POSIXLY_CORRECT") != NULL;
	}
#endif

#if ENABLE_FEATURE_MAKE_POSIX_202X
	if (!POSIX_2017) {
		path = argv[0];
		if (argv[0][0] != '/' && strchr(argv[0], '/')) {
			// Make relative path absolute
			path = newpath = realpath(argv[0], NULL);
			if (!path) {
				error("can't resolve path for %s: %s", argv[0], strerror(errno));
			}
		}
	} else {
		path = "make";
	}
#endif

	// Process options from MAKEFLAGS
	fargv = fargv0 = expand_makeflags(&fargc);
	if (fargv0) {
		opts = process_options(fargc, fargv, TRUE);
		fargv = fargv0 + optind;
	}
	// Reset getopt(3) so we can call it again
	GETOPT_RESET();

	// Process options from the command line
	opts |= process_options(argc, argv, FALSE);
	argv += optind;

	init_signal(SIGHUP);
	init_signal(SIGTERM);

	setmacro("$", "$", 0 | M_VALID);

	// Process macro definitions from the command line
	if (!ENABLE_FEATURE_MAKE_EXTENSIONS || posix)
		// In POSIX mode macros must appear before targets.
		// argv should now point to targets only.
		argv = process_macros(argv, 1);
	else
		process_macros(argv, 1);

	// Process macro definitions from MAKEFLAGS
	if (fargv) {
		process_macros(fargv, 2);
		free(fargv0[1]);
		free(fargv0);
	}

	// Process macro definitions from the environment
	process_macros(environ, 3);

	// Update MAKEFLAGS and environment
	update_makeflags();

	// Read built-in rules
	input(NULL, 0);

	setmacro("SHELL", "/bin/sh", 4);
	setmacro("MAKE", path, 4);
#if ENABLE_FEATURE_MAKE_POSIX_202X
	if (!POSIX_2017) {
		char *cwd = NULL;
		size_t len = 0;

		do {
			len += 256;
			cwd = xrealloc(cwd, len);
			if (getcwd(cwd, len)) {
				if (!useenv) {
					// Export cwd to environment, if necessary
					char *envcwd = getenv("CURDIR");
					if (envcwd && strcmp(cwd, envcwd) != 0)
						setenv("CURDIR", cwd, 1);
				}
				setmacro("CURDIR", cwd, 4);
				break;
			}
		} while (errno == ERANGE);
		free(cwd);
	}
	free((void *)newpath);
#endif

	fp = makefiles;
	if (!fp) {	// Look for a default Makefile
		if ((ifd = fopen("makefile", "r")) != NULL)
			makefile = "makefile";
		else if ((ifd = fopen("Makefile", "r")) != NULL)
			makefile = "Makefile";
		else
			error("no makefile found");
		goto read_makefile;
	}

	while (fp) {
		if (strcmp(fp->f_name, "-") == 0) {	// Can use stdin as makefile
			ifd = stdin;
			makefile = "stdin";
		} else {
			if ((ifd = fopen(fp->f_name, "r")) == NULL)
				error("can't open %s: %s", fp->f_name, strerror(errno));
			makefile = fp->f_name;
		}
		fp = fp->f_next;
 read_makefile:
		input(ifd, 0);
		fclose(ifd);
		makefile = NULL;
	}

	if (print)
		print_details();

	mark_special(".SILENT", OPT_s, N_SILENT);
	mark_special(".IGNORE", OPT_i, N_IGNORE);
	mark_special(".PRECIOUS", OPT_precious, N_PRECIOUS);
#if ENABLE_FEATURE_MAKE_POSIX_202X
	if (!POSIX_2017)
		mark_special(".PHONY", OPT_phony, N_PHONY);
#endif

	estat = 0;
	found_target = FALSE;
	for (; *argv; argv++) {
#if ENABLE_FEATURE_MAKE_EXTENSIONS
		// In POSIX mode only targets should now be in argv.
		// As an extension macros may still be present: skip them.
		if (posix || !strchr(*argv, '='))
#endif
		{
			found_target = TRUE;
			estat |= make(newname(*argv), 0);
		}
	}
	if (!found_target) {
		if (!firstname)
			error("no targets defined");
		estat = make(firstname, 0);
	}

#if ENABLE_FEATURE_CLEAN_UP
# if ENABLE_FEATURE_MAKE_POSIX_202X
	free((void *)numjobs);
# endif
	freenames();
	freemacros();
	freefiles(makefiles);
#endif

	return estat & MAKE_FAILURE;
}
