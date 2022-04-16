# Simple test harness infrastructure for BusyBox
#
# Copyright 2005 by Rob Landley
#
# License is GPLv2, see LICENSE in this directory for full license text.

# This file defines two functions, "testing" and "optional"
# and a couple more...

# The following environment variables may be set to enable optional behavior
# in "testing":
#    VERBOSE - Print the diff -u of each failed test case.
#    DEBUG - Enable command tracing.
#    SKIP - do not perform this test (this is set by "optional")
#
# The "testing" function takes five arguments:
#	$1) Test description
#	$2) Command(s) to run. May have pipes, redirects, etc
#	$3) Expected result on stdout
#	$4) Data to be written to file "input"
#	$5) Data to be written to stdin
#
# The exit value of testing is the exit value of $2 it ran.
#
# The environment variable "FAILCOUNT" contains a cumulative total of the
# number of failed tests.

# The "optional" function is used to skip certain tests, ala:
#   optional FEATURE_THINGY
#
# The "optional" function checks the environment variable "OPTIONFLAGS",
# which is either empty (in which case it always clears SKIP) or
# else contains a colon-separated list of features (in which case the function
# clears SKIP if the flag was found, or sets it to 1 if the flag was not found).

export FAILCOUNT=0
export SKIP=

# Helper for helpers. Oh my...

test x"$ECHO" != x"" || {
	ECHO="echo"
	test x"`echo -ne`" = x"" || {
		# Compile and use a replacement 'echo' which understands -e -n
		ECHO="$PWD/echo-ne"
		test -x "$ECHO" || {
			cc -o "$ECHO" echo.c || exit 1
		}
	}
	export ECHO
}

# Helper functions

optional()
{
	SKIP=
	while test "$1"; do
		case "${OPTIONFLAGS}" in
			*:$1:*) ;;
			*) SKIP=1; return ;;
		esac
		shift
	done
}

# The testing function

testing()
{
  NAME="$1"
  [ -n "$1" ] || NAME="$2"

  if [ $# -ne 5 ]
  then
    echo "Test $NAME has wrong number of arguments: $# (must be 5)" >&2
    exit 1
  fi

  [ -z "$DEBUG" ] || set -x

  if [ -n "$SKIP" ]
  then
    echo "SKIPPED: $NAME"
    return 0
  fi

  $ECHO -ne "$3" > expected
  $ECHO -ne "$4" > input
  [ -z "$VERBOSE" ] || echo ======================
  [ -z "$VERBOSE" ] || echo "echo -ne '$4' >input"
  [ -z "$VERBOSE" ] || echo "echo -ne '$5' | $2"
  $ECHO -ne "$5" | eval "$2" > actual
  RETVAL=$?

  if cmp expected actual >/dev/null 2>/dev/null
  then
    echo "PASS: $NAME"
  else
    FAILCOUNT=$(($FAILCOUNT + 1))
    echo "FAIL: $NAME"
    [ -z "$VERBOSE" ] || diff -u expected actual
  fi
  rm -f input expected actual

  [ -z "$DEBUG" ] || set +x

  return $RETVAL
}
