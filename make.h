/*
 * Include header for make
 */
#define _XOPEN_SOURCE 700
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdarg.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <ctype.h>
#include <libgen.h>
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>
#include <signal.h>

extern char **environ;

#define NORETURN __attribute__ ((__noreturn__))

// If ENABLE_FEATURE_MAKE_EXTENSIONS is non-zero some non-POSIX extensions
// are enabled.
#ifndef ENABLE_FEATURE_MAKE_EXTENSIONS
# define ENABLE_FEATURE_MAKE_EXTENSIONS 0
#endif

#if ENABLE_FEATURE_MAKE_EXTENSIONS
# define IF_FEATURE_MAKE_EXTENSIONS(...) __VA_ARGS__
# define IF_NOT_FEATURE_MAKE_EXTENSIONS(...)
#else
# define IF_FEATURE_MAKE_EXTENSIONS(...)
# define IF_NOT_FEATURE_MAKE_EXTENSIONS(...) __VA_ARGS__
#endif

// IF ENABLE_FEATURE_MAKE_POSIX_202X is non-zero POSIX 202X features
// are enabled.
//
// If ENABLE_FEATURE_MAKE_POSIX_202X is set implicitly by inheriting
// the value of ENABLE_FEATURE_MAKE_EXTENSIONS the POSIX mode enforced
// by .POSIX, PDPMAKE_POSIXLY_CORRECT or --posix remains POSIX 2017.
//
// If ENABLE_FEATURE_MAKE_POSIX_202X is set explicitly the mode
// enforced by runtime settings is POSIX 202X.
#define POSIX_2017 posix
#ifndef ENABLE_FEATURE_MAKE_POSIX_202X
# define ENABLE_FEATURE_MAKE_POSIX_202X ENABLE_FEATURE_MAKE_EXTENSIONS
#elif ENABLE_FEATURE_MAKE_POSIX_202X
# undef POSIX_2017
# define POSIX_2017 0
#endif

#if ENABLE_FEATURE_MAKE_POSIX_202X
# define IF_FEATURE_MAKE_POSIX_202X(...) __VA_ARGS__
# define IF_NOT_FEATURE_MAKE_POSIX_202X(...)
#else
# define IF_FEATURE_MAKE_POSIX_202X(...)
# define IF_NOT_FEATURE_MAKE_POSIX_202X(...) __VA_ARGS__
#endif

// If ENABLE_FEATURE_CLEAN_UP is non-zero all allocated structures are
// freed at the end of main().  This isn't necessary but it's a nice test.
#ifndef ENABLE_FEATURE_CLEAN_UP
# define ENABLE_FEATURE_CLEAN_UP 0
#endif

#define TRUE		(1)
#define FALSE		(0)
#define MAX(a,b)	((a)>(b)?(a):(b))

#define OPTSTR1 "eiknqrsSt"
#if ENABLE_FEATURE_MAKE_EXTENSIONS
#define OPTSTR2 "pf:C:"
#else
#define OPTSTR2 "pf:"
#endif

enum {
	OPTBIT_p = 9,
	OPTBIT_f,
	IF_FEATURE_MAKE_EXTENSIONS(OPTBIT_C,)
	OPTBIT_precious,
	IF_FEATURE_MAKE_POSIX_202X(OPTBIT_phony,)

	OPT_e = (1 << 0),
	OPT_i = (1 << 1),
	OPT_k = (1 << 2),
	OPT_n = (1 << 3),
	OPT_q = (1 << 4),
	OPT_r = (1 << 5),
	OPT_s = (1 << 6),
	OPT_S = (1 << 7),
	OPT_t = (1 << 8),
	// These options aren't allowed in MAKEFLAGS
	OPT_p = (1 << OPTBIT_p),
	OPT_f = (1 << OPTBIT_f),
	OPT_C = IF_FEATURE_MAKE_EXTENSIONS((1 << OPTBIT_C)) + 0,
	// The following aren't command line options and must be last
	OPT_precious = (1 << OPTBIT_precious),
	OPT_phony = IF_FEATURE_MAKE_POSIX_202X((1 << OPTBIT_phony)) + 0,
};

// Options that aren't included in MAKEFLAGS
#define OPT_MASK  (~(OPT_C | OPT_f | OPT_p | OPT_S | OPT_precious | OPT_phony))

#define useenv    (opts & OPT_e)
#define ignore    (opts & OPT_i)
#define errcont   (opts & OPT_k)
#define dryrun    (opts & OPT_n)
#define print     (opts & OPT_p)
#define quest     (opts & OPT_q)
#define norules   (opts & OPT_r)
#define silent    (opts & OPT_s)
#define dotouch   (opts & OPT_t)
#define precious  (opts & OPT_precious)

// A name.  This represents a file, either to be made, or pre-existing.
struct name {
	struct name *n_next;	// Next in the list of names
	char *n_name;			// Called
	struct rule *n_rule;	// Rules to build this (prerequisites/commands)
	struct timespec n_tim;	// Modification time of this name
	uint16_t n_flag;		// Info about the name
};

#define N_DOING		0x01	// Name in process of being built
#define N_DONE		0x02	// Name looked at
#define N_TARGET	0x04	// Name is a target
#define N_PRECIOUS	0x08	// Target is precious
#if ENABLE_FEATURE_MAKE_EXTENSIONS
#define N_DOUBLE	0x10	// Double-colon target
#else
#define N_DOUBLE	0x00	// No support for double-colon target
#endif
#define N_SILENT	0x20	// Build target silently
#define N_IGNORE	0x40	// Ignore build errors
#define N_SPECIAL	0x80	// Special target
#if ENABLE_FEATURE_MAKE_EXTENSIONS
#define N_MARK		0x100	// Mark for deduplication
#endif
#if ENABLE_FEATURE_MAKE_POSIX_202X
#define N_PHONY		0x200	// Name is a phony target
#else
#define N_PHONY		0		// No support for phony targets
#endif

// List of rules to build a target
struct rule {
	struct rule *r_next;	// Next rule
	struct depend *r_dep;	// Prerequisites for this rule
	struct cmd *r_cmd;		// Commands for this rule
};

// NOTE: the layout of the following two structures must be compatible.

// List of prerequisites for a rule
struct depend {
	struct depend *d_next;	// Next prerequisite
	struct name *d_name;	// Name of prerequisite
	int d_refcnt;			// Reference count
};

// List of commands for a rule
struct cmd {
	struct cmd *c_next;		// Next command line
	char *c_cmd;			// Text of command line
	int c_refcnt;			// Reference count
};

// Macro storage
struct macro {
	struct macro *m_next;	// Next variable
	char *m_name;			// Its name
	char *m_val;			// Its value
#if ENABLE_FEATURE_MAKE_EXTENSIONS
	bool m_simple;			// Simply-expanded macro set using :=
#endif
	bool m_flag;			// Infinite loop check
	uint8_t m_level;		// Level at which macro was created
};

// Flag passed to setmacro() to indicate that a simply-expanded macro
// is being defined.
#define M_SIMPLE 8

#define HTABSIZE 199

extern const char *myname;
extern const char *makefile;
extern struct name *namehead[HTABSIZE];
extern struct macro *macrohead[HTABSIZE];
extern struct name *firstname;
extern struct name *target;
extern uint32_t opts;
extern int ilevel;
extern int lineno;
extern int dispno;
#if ENABLE_FEATURE_MAKE_EXTENSIONS
extern bool posix;
extern bool first_line;
#endif

void print_details(void);
char *expand_macros(const char *str);
void input(FILE *fd);
struct macro *getmp(const char *name);
void setmacro(const char *name, const char *val, int level);
void freemacros(void);
void remove_target(void);
int make(struct name *np, int level);
char *splitlib(const char *name, char **member);
void modtime(struct name *np);
char *suffix(const char *name);
struct name *dyndep(struct name *np, struct rule *imprule);
char *getrules(char *s, int size);
struct name *findname(const char *name);
struct name *newname(const char *name);
struct cmd *getcmd(struct name *np);
void freenames(void);
struct depend *newdep(struct name *np, struct depend *dp);
void freedeps(struct depend *dp);
struct cmd *newcmd(char *str, struct cmd *cp);
void freecmds(struct cmd *cp);
void freerules(struct rule *rp);
void addrule(struct name *np, struct depend *dp, struct cmd *cp, int flag);
void error(const char *msg, ...) NORETURN;
void error_unexpected(const char *s) NORETURN;
void error_in_inference_rule(const char *s) NORETURN;
void warning(const char *msg, ...);
void *xmalloc(size_t len);
void *xrealloc(void *ptr, size_t len);
char *xconcat3(const char *s1, const char *s2, const char *s3);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);
char *xappendword(const char *str, const char *word);
unsigned int getbucket(const char *name);
