/*
 * Include header for make
 */
#define _XOPEN_SOURCE 700
#if defined(__sun__)
# define __EXTENSIONS__
#endif
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

#define NORETURN __attribute__ ((__noreturn__))

// Resetting getopt(3) is hopelessly platform-dependent.  If command
// line options don't work as expected you may need to tweak this.
// The default should work for GNU libc and OpenBSD.
#if defined(__FreeBSD__) || defined(__NetBSD__)
# define GETOPT_RESET() do { \
	extern int optreset; \
	optind = 1; \
	optreset = 1; \
} while (0)
#elif defined(__sun__)
# define GETOPT_RESET() (optind = 1)
#else
# define GETOPT_RESET() (optind = 0)
#endif

// Supported POSIX levels
#define STD_POSIX_2017 0
#define STD_POSIX_2024 1

// If ENABLE_FEATURE_MAKE_EXTENSIONS is non-zero some non-POSIX extensions
// are enabled.
//
#ifndef ENABLE_FEATURE_MAKE_EXTENSIONS
# define ENABLE_FEATURE_MAKE_EXTENSIONS 1
#endif

#ifndef DEFAULT_POSIX_LEVEL
# define DEFAULT_POSIX_LEVEL STD_POSIX_2024
#endif

#if ENABLE_FEATURE_MAKE_EXTENSIONS
# define IF_FEATURE_MAKE_EXTENSIONS(...) __VA_ARGS__
# define IF_NOT_FEATURE_MAKE_EXTENSIONS(...)
# define POSIX_2017 (posix && posix_level == STD_POSIX_2017)
#else
# define IF_FEATURE_MAKE_EXTENSIONS(...)
# define IF_NOT_FEATURE_MAKE_EXTENSIONS(...) __VA_ARGS__
#endif

// IF ENABLE_FEATURE_MAKE_POSIX_2024 is non-zero POSIX 2024 features
// are enabled.
#ifndef ENABLE_FEATURE_MAKE_POSIX_2024
# define ENABLE_FEATURE_MAKE_POSIX_2024 1
#endif

#if ENABLE_FEATURE_MAKE_POSIX_2024
# define IF_FEATURE_MAKE_POSIX_2024(...) __VA_ARGS__
# define IF_NOT_FEATURE_MAKE_POSIX_2024(...)
#else
# define IF_FEATURE_MAKE_POSIX_2024(...)
# define IF_NOT_FEATURE_MAKE_POSIX_2024(...) __VA_ARGS__
#endif

#if ENABLE_FEATURE_MAKE_EXTENSIONS
# define POSIX_2017 (posix && posix_level == STD_POSIX_2017)
#elif ENABLE_FEATURE_MAKE_POSIX_2024
# define POSIX_2017 FALSE
#endif

// If ENABLE_FEATURE_CLEAN_UP is non-zero all allocated structures are
// freed at the end of main().  This isn't necessary but it's a nice test.
#ifndef ENABLE_FEATURE_CLEAN_UP
# define ENABLE_FEATURE_CLEAN_UP 0
#endif

#define TRUE		(1)
#define FALSE		(0)
#define MAX(a,b)	((a)>(b)?(a):(b))

#if defined(__GLIBC__) && ENABLE_FEATURE_MAKE_EXTENSIONS
// By default GNU libc getopt(3) allows options and non-options to be
// mixed.  Turn this off in POSIX mode.  The '+' prefix in OPTSTR1 is
// otherwise unused and should be skipped.
# define OPT_OFFSET + !posix
#else
# define OPT_OFFSET
#endif

#ifdef __clang__
#pragma clang diagnostic ignored "-Wstring-plus-int"
#endif

#if ENABLE_FEATURE_MAKE_EXTENSIONS
#define OPTSTR1 "+ehij:knqrsSt"
#elif ENABLE_FEATURE_MAKE_POSIX_2024
#define OPTSTR1 "+eij:knqrsSt"
#else
#define OPTSTR1 "+eiknqrsSt"
#endif
#if ENABLE_FEATURE_MAKE_EXTENSIONS
#define OPTSTR2 "pf:C:x:"
#else
#define OPTSTR2 "pf:"
#endif

enum {
	OPTBIT_e = 0,
	IF_FEATURE_MAKE_EXTENSIONS(OPTBIT_h,)
	OPTBIT_i,
	IF_FEATURE_MAKE_POSIX_2024(OPTBIT_j,)
	OPTBIT_k,
	OPTBIT_n,
	OPTBIT_q,
	OPTBIT_r,
	OPTBIT_s,
	OPTBIT_S,
	OPTBIT_t,
	OPTBIT_p,
	OPTBIT_f,
	IF_FEATURE_MAKE_EXTENSIONS(OPTBIT_C,)
	IF_FEATURE_MAKE_EXTENSIONS(OPTBIT_x,)
	OPTBIT_precious,
	IF_FEATURE_MAKE_POSIX_2024(OPTBIT_phony,)
	IF_FEATURE_MAKE_POSIX_2024(OPTBIT_include,)
	IF_FEATURE_MAKE_POSIX_2024(OPTBIT_make,)

	OPT_e = (1 << OPTBIT_e),
	OPT_h = IF_FEATURE_MAKE_EXTENSIONS((1 << OPTBIT_h)) + 0,
	OPT_i = (1 << OPTBIT_i),
	OPT_j = IF_FEATURE_MAKE_POSIX_2024((1 << OPTBIT_j)) + 0,
	OPT_k = (1 << OPTBIT_k),
	OPT_n = (1 << OPTBIT_n),
	OPT_q = (1 << OPTBIT_q),
	OPT_r = (1 << OPTBIT_r),
	OPT_s = (1 << OPTBIT_s),
	OPT_S = (1 << OPTBIT_S),
	OPT_t = (1 << OPTBIT_t),
	// These options aren't allowed in MAKEFLAGS
	OPT_p = (1 << OPTBIT_p),
	OPT_f = (1 << OPTBIT_f),
	OPT_C = IF_FEATURE_MAKE_EXTENSIONS((1 << OPTBIT_C)) + 0,
	OPT_x = IF_FEATURE_MAKE_EXTENSIONS((1 << OPTBIT_x)) + 0,
	// The following aren't command line options and must be last
	OPT_precious = (1 << OPTBIT_precious),
	OPT_phony = IF_FEATURE_MAKE_POSIX_2024((1 << OPTBIT_phony)) + 0,
	OPT_include = IF_FEATURE_MAKE_POSIX_2024((1 << OPTBIT_include)) + 0,
	OPT_make = IF_FEATURE_MAKE_POSIX_2024((1 << OPTBIT_make)) + 0,
};

// Options in OPTSTR1 that aren't included in MAKEFLAGS
#define OPT_MASK  (~OPT_S)

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
#define doinclude (opts & OPT_include)
#define domake    (opts & OPT_make)

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
#if ENABLE_FEATURE_MAKE_EXTENSIONS || ENABLE_FEATURE_MAKE_POSIX_2024
#define N_MARK		0x100	// Mark for deduplication
#endif
#if ENABLE_FEATURE_MAKE_POSIX_2024
#define N_PHONY		0x200	// Name is a phony target
#else
#define N_PHONY		0		// No support for phony targets
#endif
#define N_INFERENCE	0x400	// Inference rule

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
	const char *c_makefile;	// Makefile in which command was defined
	int c_dispno;			// Line number within makefile
};

// Macro storage
struct macro {
	struct macro *m_next;	// Next variable
	char *m_name;			// Its name
	char *m_val;			// Its value
#if ENABLE_FEATURE_MAKE_EXTENSIONS || ENABLE_FEATURE_MAKE_POSIX_2024
	bool m_immediate;		// Immediate-expansion macro set using ::=
#endif
	bool m_flag;			// Infinite loop check
	uint8_t m_level;		// Level at which macro was created
};

// List of file names
struct file {
	struct file *f_next;
	char *f_name;
};

// Flags passed to setmacro()
#define M_IMMEDIATE  0x08	// immediate-expansion macro is being defined
#define M_VALID      0x10	// assert macro name is valid
#define M_ENVIRON    0x20	// macro imported from environment

#define HTABSIZE 199

// Constants for PRAGMA.  Order must match strings in set_pragma().
enum {
	BIT_MACRO_NAME = 0,
	BIT_TARGET_NAME,
	BIT_COMMAND_COMMENT,
	BIT_EMPTY_SUFFIX,
#if defined(__CYGWIN__)
	BIT_WINDOWS,
#endif
	BIT_POSIX_2017,
	BIT_POSIX_2024,
	BIT_POSIX_202X,

	P_MACRO_NAME = (1 << BIT_MACRO_NAME),
	P_TARGET_NAME = (1 << BIT_TARGET_NAME),
	P_COMMAND_COMMENT = (1 << BIT_COMMAND_COMMENT),
	P_EMPTY_SUFFIX = (1 << BIT_EMPTY_SUFFIX),
#if defined(__CYGWIN__)
	P_WINDOWS = (1 << BIT_WINDOWS),
#endif
};

// Status of make()
#define MAKE_FAILURE		0x01
#define MAKE_DIDSOMETHING	0x02

extern const char *myname;
extern const char *makefile;
extern struct name *namehead[HTABSIZE];
extern struct macro *macrohead[HTABSIZE];
extern struct name *firstname;
extern struct name *target;
extern uint32_t opts;
extern int lineno;
extern int dispno;
#if ENABLE_FEATURE_MAKE_POSIX_2024
extern char *numjobs;
#endif
#if ENABLE_FEATURE_MAKE_EXTENSIONS
extern bool posix;
extern bool seen_first;
extern unsigned char pragma;
extern unsigned char posix_level;
#endif

// Return TRUE if c is allowed in a POSIX 2017 macro or target name
#define ispname(c) (isalpha(c) || isdigit(c) || c == '.' || c == '_')
// Return TRUE if c is in the POSIX 'portable filename character set'
#define isfname(c) (ispname(c) || c == '-')

void print_details(void);
#if !ENABLE_FEATURE_MAKE_POSIX_2024
#define expand_macros(s, e) expand_macros(s)
#endif
#if !ENABLE_FEATURE_MAKE_EXTENSIONS
#define dyndep(n, i, p) dyndep(n, i)
#endif
char *expand_macros(const char *str, int except_dollar);
void input(FILE *fd, int ilevel);
struct macro *getmp(const char *name);
void setmacro(const char *name, const char *val, int level);
void freemacros(void);
void remove_target(void);
int make(struct name *np, int level);
char *splitlib(const char *name, char **member);
void modtime(struct name *np);
char *suffix(const char *name);
const char *is_suffix(const char *s);
char *has_suffix(const char *name, const char *suffix);
struct name *dyndep(struct name *np, struct rule *infrule, const char **ptsuff);
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
void set_pragma(const char *name);
void addrule(struct name *np, struct depend *dp, struct cmd *cp, int flag);
void diagnostic(const char *msg, ...);
void error(const char *msg, ...) NORETURN;
void error_unexpected(const char *s) NORETURN;
void error_in_inference_rule(const char *s) NORETURN;
void error_not_allowed(const char *s, const char *t);
void warning(const char *msg, ...);
void *xmalloc(size_t len);
void *xrealloc(void *ptr, size_t len);
char *xconcat3(const char *s1, const char *s2, const char *s3);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);
char *xappendword(const char *str, const char *word);
unsigned int getbucket(const char *name);
struct file *newfile(char *str, struct file *fphead);
void freefiles(struct file *fp);
int is_valid_target(const char *name);
void pragmas_from_env(void);
void pragmas_to_env(void);
