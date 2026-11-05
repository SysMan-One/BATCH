/*
**++
**  MODULE:		UTIL.C
**  IDENT:		X-02-02ECO02
**  REV:		02.02.02
**
**  FACILITY:		BATCH
**
**  ABSTRACT:		General-purpose routines: output through the FAO engine, parsing and formatting
**			of time specifications in the DCL convention, directory creation, construction of file
**			specifications with truncation checking, and guaranteed transfer to and from a
**			descriptor.
**
**  AUTHOR:		StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
**
**  CREATION DATE:	28-JUL-2026
**
**  MODIFICATION HISTORY:
**
**	28-JUL-2026	RRL	X-01-01	Initial revision.
**
**	28-JUL-2026	RRL	X-01-02	Routines UTIL$STATE_NAME, UTIL$FMT_TIME and UTIL$PARSE_TIME converted to
**				returning a status with the value delivered through an output parameter.
**				Braces removed from single statements under if.
**
**	28-JUL-2026	RRL	X-01-03	Routines return condition values of the facility instead of severity
**				codes. UTIL$LOG takes a condition value, extracts the severity and the
**				mnemonic from it and substitutes the default text from BATCHMSG.C.
**
**	28-JUL-2026	RRL	X-01-04	Comments and message texts converted to English.
**
**	28-JUL-2026	RRL	X-01-05	The subsystem was moved to UTC and the time routines were reimplemented
**				on the STARLET services UTIL$ASCTIM, UTIL$BINTIM, UTIL$GETTIM and
**				UTIL$ADD_TIMES. The local month table and the calls to localtime_r and
**				mktime were withdrawn.
**
**	28-JUL-2026	RRL	X-01-06	The failure of a C RTL routine is now reported where it occurs, naming the
**				routine and carrying its result together with errno, so that nothing of what
**				the run-time library had to say is lost on the way up. UTIL$READ_EXACT and
**				UTIL$WRITE_EXACT used to return a condition with no message at all.
**
**	28-JUL-2026	RRL	X-01-07	All formatting was moved to the FAO engine of STARLET. UTIL$MKFSPEC was
**				reimplemented on UTIL$FAOL and takes its parameters as an array, a variable
**				list being impossible to forward to the engine. UTIL$PUT was added: every
**				line the utility addresses to its user passes through it. The dependence on
**				<stdio.h> and <syslog.h> was withdrawn.
**
**	29-JUL-2026	RRL	X-01-08	Routines UTIL$PARSE_CPULIST and UTIL$FMT_CPULIST added for the /CPU
**				qualifier.
**
**	30-JUL-2026	RRL	X-01-09	The STARLET services are called by their names in the revision now
**				vendored, which carry the __util$ prefix: __UTIL$FAOL, __UTIL$ASCTIM,
**				__UTIL$BINTIM, __UTIL$GETTIM and __UTIL$ADD_TIMES.
**
**	 2-AUG-2026	RRL	X-01-10	Times are rendered by the clock of whoever reads them and an absolute
**				/AFTER is read by his clock too. The two go together: were only one of them
**				local, a time entered and then displayed would come back altered. The
**				subsystem keeps UTC within and in the journal, so a queue survives a change
**				of zone; UTIL$TZOFFSET and UTIL$LOCAL2UTC are the conversion, and both take
**				the season of the instant named rather than of the present one.
**
**	 2-AUG-2026	RRL	X-01-11	UTIL$PAGE, which pages the output where there is a terminal to hold it at,
**				and the encoding of a job record moved here from the controller: both
**				images need it, the utility to read a journal with ANALYZE.
**
**	28-AUG-2026	RRL	X-01-12	Diagnostics are issued by $PUTMSG_FAO. The message table is written for
**				the FAO engine, and the envelope %FAC-S-IDENT is supplied by the library
**				rather than carried in every text - a text that carried its own was framed
**				twice. The parameters are unchanged at the point of call: $PUTMSG_FAO casts
**				each to FAO_PRM_T itself and derives their count from the list.
**
**	 8-SEP-2026	RRL	X-01-13	UTIL$FMT_SETENT renders the attributes a SET ENTRY request carries, for
**				the confirmation of the utility and the log of the controller alike.
**
**	18-OCT-2026	RRL	X-02-02	Revision raised to X-02-02 together with the product; no change of its own.
**
**	28-OCT-2026	CLD	X-02-02ECO01	JRN$K_FSID written and read with the rest of the pin of the script.
**
**	 5-NOV-2026	RRL	X-02-02ECO02	UTIL$GETPW and UTIL$GETGROUPS: the user and the groups of a user from /etc/passwd
**				and /etc/group, without the name service (see S_ETC_READ).
**--
*/

#include	<errno.h>
#include	<fcntl.h>
#include	<stdlib.h>
#include	<string.h>
#include	<unistd.h>
#include	<sys/stat.h>
#include	<sys/types.h>

#include	"batchdef.h"
#include	"batchipc.h"
#include	"batchjrn.h"

#undef	__MODULE__
#undef	__IDENT__
#undef	__REV__
#include	"helpdef.h"
#include	"starlet.h"

#undef	__MODULE__
#undef	__IDENT__
#undef	__REV__

#define	__MODULE__	"UTIL"
#define	__IDENT__	"X-02-02ECO02"
#define	__REV__		"02.02.02"

#define	UTIL$K_SECDAY	86400	/* Seconds in a day */
#define	UTIL$K_OUTSZ	1024	/* Working buffer of UTIL$PUT */
#define	UTIL$K_TIMSZ	64	/* Working buffer for a time conversion */

/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Yields the mnemonic of a job state for display by the SHOW command.
**
**  INPUT PARAMETERS:
**	a_state	- job state code, one of the JOB$K_* values.
**
**  OUTPUT PARAMETERS:
**	a_name	- receives a pointer to a static string holding the mnemonic, not NULL. For an unknown
**		  state code the string "Unknown" is supplied.
**
**  RETURNS:
**	BATCH$_NORMAL for a known state, BATCH$_INVSTATE for an unknown one.
**--
*/
int	util$state_name (
		unsigned int	a_state,
		const char **	a_name
		)
{
	switch ( a_state )
		{
		case	JOB$K_PENDING:		*a_name = "Pending";	return	BATCH$_NORMAL;
		case	JOB$K_HOLDING:		*a_name = "Holding";	return	BATCH$_NORMAL;
		case	JOB$K_EXECUTING:	*a_name = "Executing";	return	BATCH$_NORMAL;
		case	JOB$K_ABORTING:		*a_name = "Aborting";	return	BATCH$_NORMAL;
		case	JOB$K_RETAINED:		*a_name = "Retained";	return	BATCH$_NORMAL;
		}

	*a_name = "Unknown";

	return	BATCH$_INVSTATE;
}

/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Yields the offset of the local time zone from UTC, in seconds, as it stands at a given
**	instant.
**
**	The instant matters: an offset is not a property of a place but of a place at a time, and a
**	job held until the far side of a change of season is to be rendered by the rule that will be
**	in force then rather than by the one in force now.
**
**  INPUT PARAMETERS:
**	a_time	- the instant, in seconds since the epoch.
**
**  OUTPUT PARAMETERS:
**	None.
**
**  RETURNS:
**	The offset in seconds, positive to the east of Greenwich. Zero where the zone cannot be
**	determined, which renders the time as UTC and is the behaviour this subsystem had before.
**--
*/
long long	util$tzoffset (
			long long	a_time
			)
{
struct tm	l_tm;
time_t		l_t = (time_t) a_time;

	if ( !localtime_r(&l_t, &l_tm) )
		return	0;

	return	(long long) l_tm.tm_gmtoff;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Formats a point in time as dd-MMM-yyyy hh:mm:ss. The conversion is performed by the STARLET
**	service UTIL$ASCTIM, which yields the OpenVMS representation dd-MMM-yyyy hh:mm:ss.cc; the
**	fraction of a second is dropped, since neither the queue display nor the journal carries it.
**
**	The subsystem operates in UTC, so no timezone conversion takes place.
**
**	A zero time, meaning that the event has not occurred, is rendered as hyphens so that the
**	columns of the tabular output of the SHOW command do not lose their alignment.
**
**  INPUT PARAMETERS:
**	a_time	- point in time in seconds of the epoch; zero denotes the absence of a value.
**	a_bufsz	- size of the output buffer in bytes, at least 21.
**
**  OUTPUT PARAMETERS:
**	a_buf	- buffer receiving the result, not NULL.
**
**  RETURNS:
**	BATCH$_NORMAL, or BATCH$_SMALLBUF if the buffer is manifestly too small.
**--
*/
int	util$fmt_time (
		long long	a_time,
		char *		a_buf,
		size_t		a_bufsz
		)
{
fao_time_t	l_tim;
fao_desc_t	l_dsc;
unsigned short	l_len;
char		l_work [UTIL$K_TIMSZ], *l_dot;

	if ( a_bufsz < 21 )
		return	BATCH$_SMALLBUF;

	if ( !a_time )
		{
		memset(a_buf, '-', 20);
		a_buf[20] = '\0';

		return	BATCH$_NORMAL;
		}

	/*
	**  The time is rendered in the time zone of whoever is reading it. UTIL$ASCTIM works in UTC,
	**  so the offset in force at that instant is added before it is called: the reader compares
	**  what he sees against his own clock, and a job that appears to have been queued three hours
	**  ago when it was queued a moment since is a puzzle he did not ask for.
	**
	**  The offset is taken at the instant being rendered rather than at the present one, so a
	**  time on the far side of a change of season is rendered by the rule that will be in force
	**  then rather than by the one in force now.
	*/
	l_tim			= (fao_time_t) (a_time + util$tzoffset((long long) a_time));
	l_len			= 0;
	l_dsc.dsc$w_length	= (unsigned short) sizeof(l_work);
	l_dsc.dsc$b_dtype	= 0;
	l_dsc.dsc$b_class	= 0;
	l_dsc.dsc$a_pointer	= l_work;

	if ( __util$asctim(&l_len, &l_dsc, &l_tim, 0) != STS$K_SUCCESS )
		return	BATCH$_INVTIME;

	l_work[(l_len < sizeof(l_work)) ? l_len : sizeof(l_work) - 1] = '\0';

	/* Drop the hundredths of a second yielded by the OpenVMS representation */
	if ( (l_dot = strchr(l_work, '.')) )
		*l_dot = '\0';

	return	util$mkfspec(a_buf, a_bufsz, "!AZ", (const fao_prm_t []) { (fao_prm_t) l_work }, 1);
}

/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Takes a wall clock reading that was interpreted as UTC and yields the instant at which that
**	same reading shows on the local clock.
**
**	Subtracting the offset would not do: the offset in force at the instant named is not always
**	the one in force at the instant of asking, and a reading that falls in the hour a season
**	change removes has no instant at all. MKTIME with TM_ISDST left at -1 settles both, choosing
**	the rule of the day named and resolving an impossible reading to the nearest real one.
**
**  INPUT PARAMETERS:
**	a_time	- the instant a UTC reading of the wall clock produced.
**
**  OUTPUT PARAMETERS:
**	None.
**
**  RETURNS:
**	The instant of the same reading taken locally, or the argument unchanged where the conversion
**	fails - which leaves the time as UTC, the behaviour this subsystem had before.
**--
*/
long long	util$local2utc (
			long long	a_time
			)
{
struct tm	l_tm;
time_t		l_t = (time_t) a_time, l_res;

	if ( !gmtime_r(&l_t, &l_tm) )
		return	a_time;

	l_tm.tm_isdst = -1;

	if ( (time_t) -1 == (l_res = mktime(&l_tm)) )
		return	a_time;

	return	(long long) l_res;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Parses a time specification in the DCL convention accepted by this subsystem. Three forms are
**	recognized:
**
**	  delta time	"+[dddd-]hh[:mm[:ss]]"	omitted fields are zero, so "+:30" is thirty minutes
**	  absolute	dd-MMM-yyyy[:hh:mm:ss]	omitted time fields are zero
**	  keywords	NOW, TODAY, TOMORROW
**
**	The conversion itself is performed by the STARLET service UTIL$BINTIM, whose grammar differs
**	from the one above: it separates the day count from the time of day by a blank and knows
**	nothing of the leading plus sign or of the keywords. This routine is therefore a syntax
**	adapter - it rewrites the specification into the form UTIL$BINTIM accepts and leaves the
**	arithmetic to the library.
**
**	UTIL$BINTIM yields a delta time as a negative value, in the OpenVMS convention; it is turned
**	into a point in time by UTIL$ADD_TIMES against the current time from UTIL$GETTIM.
**
**	The subsystem operates in UTC, so a keyword denoting midnight is midnight UTC.
**
**  INPUT PARAMETERS:
**	a_str	- the string to be parsed, neither NULL nor empty.
**
**  OUTPUT PARAMETERS:
**	a_time	- receives the point in time in seconds of the epoch, not NULL. On a parsing error the
**		  contents are left unchanged.
**
**  RETURNS:
**	BATCH$_NORMAL, or BATCH$_INVTIME on a syntax error.
**--
*/
int	util$parse_time (
		const char *	a_str,
		long long *	a_time
		)
{
fao_time_t	l_now, l_delta, l_res;
long long	l_off;
char		l_work [UTIL$K_TIMSZ], l_conv [UTIL$K_TIMSZ], *l_dash, *l_fld [4], *l_sav;
int		l_hh, l_mm, l_ss, l_i, l_n, l_days;

	if ( (!a_str) || (!*a_str) )
		return	BATCH$_INVTIME;

	if ( __util$gettim(&l_now) != STS$K_SUCCESS )
		return	BATCH$_INVTIME;

	/*
	**  Keywords. TODAY and TOMORROW denote midnight at the start of the respective day; in UTC
	**  that is simply the current time truncated to a whole number of days.
	*/
	if ( !strcasecmp(a_str, "NOW") )
		{
		*a_time = (long long) l_now;

		return	BATCH$_NORMAL;
		}

	if ( (!strcasecmp(a_str, "TODAY")) || (!strcasecmp(a_str, "TOMORROW")) )
		{
		/* The midnight meant is the one the enquirer's day begins with, not Greenwich's */
		l_off	= util$tzoffset((long long) l_now);
		*a_time	= (((long long) l_now + l_off) / UTIL$K_SECDAY) * UTIL$K_SECDAY - l_off;

		if ( !strcasecmp(a_str, "TOMORROW") )
			*a_time += UTIL$K_SECDAY;

		/* A change of season between now and then shifts the midnight by the difference */
		*a_time -= (util$tzoffset(*a_time) - l_off);

		return	BATCH$_NORMAL;
		}

	/*
	**  Delta time. The day field is separated at the hyphen first, then the remainder is split at
	**  the colons. An empty field denotes zero, which yields the form "+:30" for thirty minutes.
	**  The result is reassembled as "dddd hh:mm:ss" for UTIL$BINTIM.
	*/
	if ( *a_str == '+' )
		{
		if ( $IS_FAILURE(util$mkfspec(l_work, sizeof(l_work), "!AZ",
					(const fao_prm_t []) { (fao_prm_t) (a_str + 1) }, 1)) )
			return	BATCH$_INVTIME;
		l_days	= 0;
		l_sav	= l_work;
		l_dash	= strchr(l_work, '-');

		if ( l_dash )
			{
			*l_dash	= '\0';
			l_days	= atoi(l_work);
			l_sav	= l_dash + 1;
			}

		for ( l_i = 0; l_i < 4; l_i++ )
			l_fld[l_i] = NULL;

		/*
		**  The split is done by hand, because strtok_r collapses empty fields. It is performed in
		**  place, on the remainder of l_work: copying the remainder onto the head of the same
		**  buffer would be an overlapping copy, that is, undefined behaviour.
		*/
		l_n = 0;

		while ( (l_n < 3) && (l_sav) )
			{
			l_fld[l_n++] = l_sav;
			l_sav = strchr(l_sav, ':');

			if ( l_sav )
				*l_sav++ = '\0';
			}

		l_hh = (l_n > 0) ? atoi(l_fld[0]) : 0;
		l_mm = (l_n > 1) ? atoi(l_fld[1]) : 0;
		l_ss = (l_n > 2) ? atoi(l_fld[2]) : 0;

		if ( $IS_FAILURE(util$mkfspec(l_conv, sizeof(l_conv), "!SL !SL:!2ZL:!2ZL",
					(const fao_prm_t []) { (fao_prm_t) l_days, (fao_prm_t) l_hh,
								(fao_prm_t) l_mm, (fao_prm_t) l_ss }, 4)) )
			return	BATCH$_INVTIME;

		if ( __util$bintim(l_conv, &l_delta) != STS$K_SUCCESS )
			return	BATCH$_INVTIME;

		if ( __util$add_times(&l_now, &l_delta, &l_res) != STS$K_SUCCESS )
			return	BATCH$_INVTIME;

		*a_time = (long long) l_res;

		return	BATCH$_NORMAL;
		}

	/*
	**  Absolute time of the form dd-MMM-yyyy[:hh:mm:ss]. UTIL$BINTIM separates the date from the
	**  time of day by a blank, so the first colon following the year is replaced.
	*/
	if ( $IS_FAILURE(util$mkfspec(l_conv, sizeof(l_conv), "!AZ",
				(const fao_prm_t []) { (fao_prm_t) a_str }, 1)) )
		return	BATCH$_INVTIME;

	if ( (l_sav = strchr(l_conv, ':')) )
		*l_sav = ' ';

	if ( __util$bintim(l_conv, &l_res) != STS$K_SUCCESS )
		return	BATCH$_INVTIME;

	/* A negative result means the string was taken for a delta time, which is not admissible here */
	if ( l_res < 0 )
		return	BATCH$_INVTIME;

	/*
	**  What the enquirer wrote is a reading of his own clock, and UTIL$BINTIM read it as UTC. The
	**  wall clock it produced is therefore recovered and asked for again as a local one: MKTIME
	**  with TM_ISDST left at -1 resolves the season itself, which subtracting a fixed offset would
	**  not - the offset in force at the instant named is not always the one in force now.
	*/
	*a_time = util$local2utc((long long) l_res);

	return	BATCH$_NORMAL;
}

/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Parses a processor list in the notation taskset(1) uses - a comma-separated series of numbers
**	and of ranges, "0-3,5,8-11" - and builds the affinity mask it names.
**
**	An empty list is not admissible: a mask naming no processor would leave the job unable to run
**	anywhere, which the caller is unlikely to have meant.
**
**  INPUT PARAMETERS:
**	a_str	- the list, not NULL.
**
**  OUTPUT PARAMETERS:
**	a_set	- receives the mask, not NULL. On a parsing error the contents are left unchanged.
**	a_count	- receives the number of processors named, not NULL.
**
**  RETURNS:
**	BATCH$_NORMAL or BATCH$_IVCPU.
**--
*/
int	util$parse_cpulist (
		const char *	a_str,
		cpu_set_t *	a_set,
		int *		a_count
		)
{
cpu_set_t	l_set;
const char *	l_p;
long		l_lo, l_hi, l_i;
char *		l_end;
int		l_n;

	if ( (!a_str) || (!*a_str) )
		return	BATCH$_IVCPU;

	CPU_ZERO(&l_set);
	l_n	= 0;
	l_p	= a_str;

	while ( *l_p )
		{
		l_lo = strtol(l_p, &l_end, 10);

		if ( (l_end == l_p) || (l_lo < 0) || (l_lo >= CPU_SETSIZE) )
			return	BATCH$_IVCPU;

		l_p	= l_end;
		l_hi	= l_lo;

		if ( *l_p == '-' )
			{
			l_hi = strtol(l_p + 1, &l_end, 10);

			if ( (l_end == l_p + 1) || (l_hi < l_lo) || (l_hi >= CPU_SETSIZE) )
				return	BATCH$_IVCPU;

			l_p = l_end;
			}

		for ( l_i = l_lo; l_i <= l_hi; l_i++ )
			{
			if ( !CPU_ISSET((int) l_i, &l_set) )
				l_n++;

			CPU_SET((int) l_i, &l_set);
			}

		if ( !*l_p )
			break;

		if ( *l_p != ',' )
			return	BATCH$_IVCPU;

		l_p++;
		}

	if ( !l_n )
		return	BATCH$_IVCPU;

	*a_set		= l_set;
	*a_count	= l_n;

	return	BATCH$_NORMAL;
}

/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Renders an affinity mask in the notation UTIL$PARSE_CPULIST accepts, collapsing runs of
**	consecutive processors into ranges.
**
**  INPUT PARAMETERS:
**	a_set	- the mask, not NULL.
**	a_bufsz	- size of the output buffer in bytes.
**
**  OUTPUT PARAMETERS:
**	a_buf	- buffer receiving the result, not NULL. An empty mask yields an empty string.
**
**  RETURNS:
**	BATCH$_NORMAL or BATCH$_BUFOVF.
**--
*/
int	util$fmt_cpulist (
		const cpu_set_t *	a_set,
		char *			a_buf,
		size_t			a_bufsz
		)
{
char	l_work [BATCH$K_CPUSZ * 4], l_part [32];
size_t	l_len;
int	l_i, l_lo, l_hi;

	l_work[0]	= '\0';
	l_len		= 0;
	l_i		= 0;

	while ( l_i < CPU_SETSIZE )
		{
		if ( !CPU_ISSET(l_i, a_set) )
			{
			l_i++;
			continue;
			}

		for ( l_lo = l_i; (l_i < CPU_SETSIZE) && (CPU_ISSET(l_i, a_set)); l_i++ )
			;

		l_hi = l_i - 1;

		if ( l_lo == l_hi )
			util$mkfspec(l_part, sizeof(l_part), "!AZ!SL",
					(const fao_prm_t []) { (fao_prm_t) ((l_len) ? "," : ""),
								(fao_prm_t) l_lo }, 2);
		else	util$mkfspec(l_part, sizeof(l_part), "!AZ!SL-!SL",
					(const fao_prm_t []) { (fao_prm_t) ((l_len) ? "," : ""),
								(fao_prm_t) l_lo, (fao_prm_t) l_hi }, 3);

		if ( (l_len + strlen(l_part)) >= sizeof(l_work) )
			return	BATCH$_BUFOVF;

		memcpy(l_work + l_len, l_part, strlen(l_part) + 1);
		l_len += strlen(l_part);
		}

	if ( l_len >= a_bufsz )
		return	BATCH$_BUFOVF;

	memcpy(a_buf, l_work, l_len + 1);

	return	BATCH$_NORMAL;
}

/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Creates a directory together with every missing intermediate directory, the functional
**	equivalent of mkdir -p. An already existing directory is not treated as an error.
**
**  INPUT PARAMETERS:
**	a_path	- absolute or relative directory path, not NULL.
**	a_mode	- access mode for newly created directories.
**
**  OUTPUT PARAMETERS:
**	None.
**
**  RETURNS:
**	BATCH$_NORMAL or BATCH$_MKDIRERR.
**--
*/
int	util$mkpath (
		const char *	a_path,
		mode_t		a_mode
		)
{
char	l_work [BATCH$K_PATHSZ], *l_p;
int	l_rc;

	if ( $IS_FAILURE(util$mkfspec(l_work, sizeof(l_work), "!AZ",
				(const fao_prm_t []) { (fao_prm_t) a_path }, 1)) )
		return	BATCH$_BUFOVF;

	/*
	**  Walk the string from left to right, temporarily terminating it at each separator and
	**  creating the level so far. The walk starts at the second character so that no attempt is
	**  made to create the root.
	*/
	for ( l_p = l_work + 1; *l_p; l_p++ )
		{
		if ( *l_p != '/' )
			continue;

		*l_p = '\0';

		if ( (0 > (l_rc = mkdir(l_work, a_mode))) && (errno != EEXIST) )
			return	$PUTMSG_FAO(BATCH$_MKDIRERR, l_work, l_rc, errno, strerror(errno));

		*l_p = '/';
		}

	if ( (0 > (l_rc = mkdir(l_work, a_mode))) && (errno != EEXIST) )
		return	$PUTMSG_FAO(BATCH$_MKDIRERR, l_work, l_rc, errno, strerror(errno));

	return	BATCH$_NORMAL;
}

/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Builds a file specification with the FAO engine, checking for truncation. It is a routine of
**	its own because silent truncation of a path leads to work on the wrong file, which is an error
**	rather than a warning and must abort the operation; the engine reports the overflow, and this
**	routine turns it into a condition of the facility and issues the diagnostic.
**
**	The parameters are passed as an array rather than a variable list: the engine itself accepts
**	them that way through UTIL$FAOL, and a variable list could not be forwarded to it. A caller
**	states them with a compound literal.
**
**	The specification is assembled through an intermediate buffer, so the source and destination
**	may belong to the same object: this permits the job log specification to be built directly in
**	the BATCH$JOB descriptor from other fields of that same descriptor.
**
**  INPUT PARAMETERS:
**	a_bufsz	 - size of the output buffer in bytes.
**	a_ctlstr - FAO control string, not NULL.
**	a_prm	 - array of parameters, each cast to fao_prm_t by the caller. May be NULL when the
**		   control string calls for none.
**	a_prmcnt - number of parameters in the array.
**
**  OUTPUT PARAMETERS:
**	a_buf	- buffer receiving the result, not NULL. On truncation the contents are left unchanged.
**
**  RETURNS:
**	BATCH$_NORMAL, or BATCH$_BUFOVF if the result does not fit in a_bufsz bytes.
**--
*/
int	util$mkfspec (
		char *			a_buf,
		size_t			a_bufsz,
		const char *		a_ctlstr,
		const fao_prm_t *	a_prm,
		int			a_prmcnt
		)
{
fao_desc_t	l_dsc;
unsigned short	l_len;
char		l_work [BATCH$K_PATHSZ * 2];
int		l_sts;

	l_len			= 0;
	l_dsc.dsc$w_length	= (unsigned short) sizeof(l_work);
	l_dsc.dsc$b_dtype	= 0;
	l_dsc.dsc$b_class	= 0;
	l_dsc.dsc$a_pointer	= l_work;

	l_sts = __util$faol(a_ctlstr, &l_len, &l_dsc, a_prm, a_prmcnt);

	if ( (l_sts != STS$K_SUCCESS) || ((size_t) l_len >= a_bufsz) )
		return	$PUTMSG_FAO(BATCH$_BUFOVF, (int) l_len, a_bufsz);

	memcpy(a_buf, l_work, (size_t) l_len);
	a_buf[l_len] = '\0';

	return	BATCH$_NORMAL;
}

/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Turns paging of the output on, and off again.
**
**	Paging is a courtesy to a reader at a terminal and a nuisance to a program reading a pipe, so
**	it is asked for and is granted only where there is a terminal to hold at: a request made when
**	the output has been redirected is quietly disregarded rather than filling a file with the
**	prompts nobody will answer.
**
**  INPUT PARAMETERS:
**	a_on	- non-zero to page the output that follows, zero to stop and flush what is held.
**
**  OUTPUT PARAMETERS:
**	None.
**
**  RETURNS:
**	BATCH$_NORMAL.
**--
*/
static	HELP$OUT	g_pageout;
static	int		g_paging = 0;

int	util$page (
		int	a_on
		)
{
unsigned	l_width = 80, l_height = 24;

	if ( !a_on )
		{
		if ( g_paging )
			help$flush(&g_pageout);

		g_paging = 0;

		return	BATCH$_NORMAL;
		}

	if ( !isatty(STDOUT_FILENO) )
		return	BATCH$_NORMAL;

	help$getwinsz(STDOUT_FILENO, &l_width, &l_height);
	help$out_init(&g_pageout, STDOUT_FILENO, STDIN_FILENO, 1, l_width);

	g_paging = 1;

	return	BATCH$_NORMAL;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Formats a line with the FAO engine and writes it to the standard output. Every message the
**	utility addresses to its user passes through here, so that no formatting of this facility is
**	left to the run-time library.
**
**	The line separator is written literally rather than by the !/ directive: that directive yields
**	the carriage return and line feed pair of OpenVMS, which is not what a Unix stream expects.
**
**  INPUT PARAMETERS:
**	a_ctlstr - FAO control string, not NULL.
**	a_prm	 - array of parameters, each cast to fao_prm_t by the caller. May be NULL.
**	a_prmcnt - number of parameters in the array.
**
**  OUTPUT PARAMETERS:
**	None; the result is written to the standard output.
**
**  RETURNS:
**	BATCH$_NORMAL, BATCH$_BUFOVF if the line does not fit the working buffer, or BATCH$_IOERR.
**--
*/
int	util$put (
		const char *		a_ctlstr,
		const fao_prm_t *	a_prm,
		int			a_prmcnt
		)
{
fao_desc_t	l_dsc;
unsigned short	l_len;
char		l_work [UTIL$K_OUTSZ];
int		l_sts;

	l_len			= 0;
	l_dsc.dsc$w_length	= (unsigned short) sizeof(l_work);
	l_dsc.dsc$b_dtype	= 0;
	l_dsc.dsc$b_class	= 0;
	l_dsc.dsc$a_pointer	= l_work;

	l_sts = __util$faol(a_ctlstr, &l_len, &l_dsc, a_prm, a_prmcnt);

	if ( l_sts != STS$K_SUCCESS )
		return	$PUTMSG_FAO(BATCH$_BUFOVF, (int) l_len, sizeof(l_work));

	/*
	**  Where paging was asked for, the line goes to the output layer of the HELP facility, which
	**  counts the lines and holds at the foot of a screen. Routing it here rather than at every
	**  place that writes a line keeps the two ways of writing from drifting apart, there being
	**  some thirty such places and the display of a queue being made of all of them.
	*/
	if ( g_paging )
		return	(1 & help$put_raw(&g_pageout, l_len, l_work)) ? BATCH$_NORMAL : BATCH$_IOERR;

	return	util$write_exact(STDOUT_FILENO, l_work, (size_t) l_len);
}

/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Reads exactly a_len bytes from a descriptor. Short reads, which are unavoidable on a socket,
**	and interruption of the call by a signal are handled.
**
**  INPUT PARAMETERS:
**	a_fd	- descriptor of a socket or a file.
**	a_len	- number of bytes required.
**
**  OUTPUT PARAMETERS:
**	a_buf	- receiving buffer of at least a_len bytes, not NULL.
**
**  RETURNS:
**	BATCH$_NORMAL when all a_len bytes have been obtained. BATCH$_ENDOFDATA if the far end closed
**	the connection before the required amount was transferred, BATCH$_IOERR on a transfer error.
**--
*/
int	util$read_exact (
		int	a_fd,
		void *	a_buf,
		size_t	a_len
		)
{
char *	l_p;
ssize_t	l_rc;
size_t	l_rest;

	l_p	= (char *) a_buf;
	l_rest	= a_len;

	while ( l_rest )
		{
		l_rc = read(a_fd, l_p, l_rest);

		/* Interruption by a signal is not an error: the call is repeated */
		if ( (l_rc == -1) && (errno == EINTR) )
			continue;

		/*
		**  Zero denotes closure by the far end, a negative value an error. Both are reported
		**  here, where the outcome of the call and errno are still those of the failure itself.
		*/
		if ( !l_rc )
			return	$PUTMSG_FAO(BATCH$_ENDOFDATA, "read", (int) l_rc, a_len - l_rest, a_len);

		if ( l_rc < 0 )
			return	$PUTMSG_FAO(BATCH$_IOERR, "read", (int) l_rc, errno, strerror(errno));

		l_p	+= l_rc;
		l_rest	-= (size_t) l_rc;
		}

	return	BATCH$_NORMAL;
}

/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Reads a small text file whole into memory: /etc/passwd and /etc/group are read this way.
**
**	The images of this product are linked statically. A static GNU C library that is asked about
**	a user by GETPWNAM, GETPWUID or INITGROUPS goes on to load the name service modules of the host
**	(libnss_systemd and the like) at run time, and those are built against another library than
**	the one in the image: the controller fell with SIGSEGV on a name that was not in the file.
**	The two files are therefore read and taken apart here, and the name service is never asked.
**
**  INPUT PARAMETERS:
**	a_path	- file specification, not NULL.
**
**  OUTPUT PARAMETERS:
**	a_buf	- receives the buffer, terminated with a NUL, to be released with FREE(); not NULL.
**	a_len	- receives the number of octets read, not NULL.
**
**  RETURNS:
**	BATCH$_NORMAL, BATCH$_IOERR on a failure of the C RTL (reported here, with errno).
**--
*/
static	int	s_etc_read (
		const char *	a_path,
		char **		a_buf,
		size_t *	a_len
		)
{
char *	l_buf, * l_new;
size_t	l_len = 0, l_cap = 4096;
ssize_t	l_rc;
int	l_fd;

	if ( 0 > (l_fd = open(a_path, O_RDONLY)) )
		return	$PUTMSG_FAO(BATCH$_IOERR, "open", l_fd, errno, strerror(errno));

	if ( !(l_buf = malloc(l_cap + 1)) )
		{
		close(l_fd);
		return	$PUTMSG_FAO(BATCH$_IOERR, "malloc", 0, errno, strerror(errno));
		}

	for ( ; ; )
		{
		if ( l_len == l_cap )
			{
			if ( !(l_new = realloc(l_buf, (l_cap <<= 1) + 1)) )
				{
				free(l_buf);
				close(l_fd);
				return	$PUTMSG_FAO(BATCH$_IOERR, "realloc", 0, errno, strerror(errno));
				}

			l_buf = l_new;
			}

		l_rc = read(l_fd, l_buf + l_len, l_cap - l_len);

		if ( (l_rc == -1) && (errno == EINTR) )
			continue;

		if ( l_rc < 0 )
			{
			free(l_buf);
			close(l_fd);
			return	$PUTMSG_FAO(BATCH$_IOERR, "read", (int) l_rc, errno, strerror(errno));
			}

		if ( !l_rc )
			break;

		l_len += (size_t) l_rc;
		}

	close(l_fd);

	l_buf[l_len]	= '\0';
	*a_buf		= l_buf;
	*a_len		= l_len;

	return	BATCH$_NORMAL;
}

/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Cuts a line of a colon-separated file into its fields in place: each colon is replaced
**	with a NUL and the field is pointed to by a_fld.
**
**  INPUT PARAMETERS:
**	a_line	- the line, terminated with a NUL, modified.
**	a_max	- the room in a_fld.
**
**  OUTPUT PARAMETERS:
**	a_fld	- pointers to the fields.
**
**  RETURNS:
**	The number of the fields found.
**--
*/
static	int	s_etc_split (
		char *	a_line,
		char **	a_fld,
		int	a_max
		)
{
int	l_cnt = 0;

	for ( a_fld[l_cnt++] = a_line; *a_line; a_line++ )
		{
		if ( (*a_line == ':') && (l_cnt < a_max) )
			{
			*a_line = '\0';
			a_fld[l_cnt++] = a_line + 1;
			}
		}

	return	l_cnt;
}

/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Converts a field of /etc/passwd or /etc/group to a number: decimal digits only, not empty.
**
**  INPUT PARAMETERS:
**	a_str	- the field.
**
**  OUTPUT PARAMETERS:
**	a_val	- receives the number.
**
**  RETURNS:
**	1 if the field is a number, 0 if not.
**--
*/
static	int	s_etc_num (
		const char *	a_str,
		unsigned long *	a_val
		)
{
char *	l_end;

	if ( !*a_str )
		return	0;

	errno	= 0;
	*a_val	= strtoul(a_str, &l_end, 10);

	return	(!*l_end) && (!errno);
}

/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Finds a user in /etc/passwd, by name or by number, without asking the name service.
**
**	Only the local file is consulted: a user that lives in LDAP, SSSD or another directory is
**	not seen, and /USER names such a user in vain. The lines of the NIS compat form (+name, -name)
**	and the comments are passed over.
**
**  INPUT PARAMETERS:
**	a_name	- the name of the user, or NULL to look up by number.
**	a_uid	- the number of the user, looked at when a_name is NULL.
**
**  OUTPUT PARAMETERS:
**	a_pw	- receives the name, the home directory, the user and the group number, not NULL.
**
**  RETURNS:
**	BATCH$_NORMAL when found. BATCH$_NOSUCHUSER when there is no such user: no message is issued,
**	it is the caller who knows whom to name. BATCH$_IOERR when the file cannot be read.
**--
*/
int	util$getpw (
		const char *	a_name,
		int		a_uid,
		UTIL$PWENT *	a_pw
		)
{
char *		l_buf, * l_cur, * l_nxt, * l_fld [8];
size_t		l_len;
unsigned long	l_uid, l_gid;
int		l_sts, l_found = 0;

	if ( $IS_FAILURE(l_sts = s_etc_read("/etc/passwd", &l_buf, &l_len)) )
		return	l_sts;

	for ( l_cur = l_buf; (*l_cur) && (!l_found); l_cur = l_nxt )
		{
		if ( (l_nxt = strchr(l_cur, '\n')) )
			*l_nxt++ = '\0';
		else	l_nxt = l_cur + strlen(l_cur);

		if ( (*l_cur == '#') || (*l_cur == '+') || (*l_cur == '-') || (*l_cur == '\0') )
			continue;

		if ( 6 > s_etc_split(l_cur, l_fld, 7) )
			continue;

		if ( !s_etc_num(l_fld[2], &l_uid) || !s_etc_num(l_fld[3], &l_gid) )
			continue;

		if ( a_name ? strcmp(l_fld[0], a_name) : (l_uid != (unsigned long) a_uid) )
			continue;

		util$mkfspec(a_pw->pw_name, sizeof(a_pw->pw_name), "!AZ", (const fao_prm_t []) { (fao_prm_t) l_fld[0] }, 1);
		util$mkfspec(a_pw->pw_dir, sizeof(a_pw->pw_dir), "!AZ", (const fao_prm_t []) { (fao_prm_t) l_fld[5] }, 1);
		a_pw->pw_uid	= (uid_t) l_uid;
		a_pw->pw_gid	= (gid_t) l_gid;
		l_found		= 1;
		}

	free(l_buf);

	return	(l_found) ? BATCH$_NORMAL : BATCH$_NOSUCHUSER;
}

/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Builds the list of the groups of a user from /etc/group, as INITGROUPS would, but without
**	asking the name service: the primary group first, then every group that names the user among
**	its members.
**
**  INPUT PARAMETERS:
**	a_name	- the name of the user.
**	a_gid	- the primary group of the user.
**	a_max	- the room in a_list.
**
**  OUTPUT PARAMETERS:
**	a_list	- receives the group numbers, not NULL.
**	a_cnt	- receives their count, not NULL.
**
**  RETURNS:
**	BATCH$_NORMAL, BATCH$_BUFOVF when there are more groups than a_max, BATCH$_IOERR when the file
**	cannot be read.
**--
*/
int	util$getgroups (
		const char *	a_name,
		gid_t		a_gid,
		gid_t *		a_list,
		int		a_max,
		int *		a_cnt
		)
{
char *		l_buf, * l_cur, * l_nxt, * l_mem, * l_end, * l_fld [5];
size_t		l_len, l_nlen = strlen(a_name);
unsigned long	l_gid;
int		l_sts, l_cnt = 0, l_dup;

	if ( $IS_FAILURE(l_sts = s_etc_read("/etc/group", &l_buf, &l_len)) )
		return	l_sts;

	a_list[l_cnt++] = a_gid;

	for ( l_cur = l_buf; *l_cur; l_cur = l_nxt )
		{
		if ( (l_nxt = strchr(l_cur, '\n')) )
			*l_nxt++ = '\0';
		else	l_nxt = l_cur + strlen(l_cur);

		if ( (*l_cur == '#') || (*l_cur == '+') || (*l_cur == '-') || (*l_cur == '\0') )
			continue;

		if ( 4 > s_etc_split(l_cur, l_fld, 4) )
			continue;

		if ( !s_etc_num(l_fld[2], &l_gid) )
			continue;

		/* The member list is a comma-separated one: the name has to match whole */
		for ( l_mem = l_fld[3]; *l_mem; l_mem = (*l_end) ? l_end + 1 : l_end )
			{
			l_end = l_mem + strcspn(l_mem, ",");

			if ( ((size_t) (l_end - l_mem) != l_nlen) || memcmp(l_mem, a_name, l_nlen) )
				continue;

			l_dup = 0;

			for (int i = 0; i < l_cnt; i++)
				l_dup |= (a_list[i] == (gid_t) l_gid);

			if ( l_dup )
				break;

			if ( l_cnt == a_max )
				{
				free(l_buf);

				return	BATCH$_BUFOVF;
				}

			a_list[l_cnt++] = (gid_t) l_gid;
			break;
			}
		}

	free(l_buf);
	*a_cnt = l_cnt;

	return	BATCH$_NORMAL;
}

/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Encodes a job descriptor into a self-describing record for the queue journal.
**
**	Every field is written with a tag of its own, so a journal outlives the layout of BATCH$JOB.
**	A field holding its default is written all the same: a record that states what it means costs
**	a few octets and spares a reader the guessing.
**
**  INPUT PARAMETERS:
**	a_job	- the descriptor, not NULL.
**	a_bufsz	- size of the buffer.
**
**  OUTPUT PARAMETERS:
**	a_buf	- receives the record, not NULL.
**	a_len	- receives its length.
**
**  RETURNS:
**	BATCH$_NORMAL, or BATCH$_BUFOVF where the record does not fit.
**--
*/
int	util$job_encode (
			const BATCH$JOB *	a_job,
			void *			a_buf,
			size_t			a_bufsz,
			unsigned *		a_len
			)
{
AVPROTO_PDU *	l_pdu = (AVPROTO_PDU *) a_buf;
unsigned int	l_u32;
long long	l_i64;
unsigned	l_ulen = 0, l_csr = 0, l_seq = 0;
int		l_i, l_sts;

	if ( $IS_FAILURE(l_sts = __tlv$hset(l_pdu, (unsigned char *) BATCHJRN$K_SIG, 0, 0)) )
		return	l_sts;

	#define	$PUT_L(tag, v)	{ l_u32 = (unsigned int) (v); \
				if ( $IS_FAILURE(l_sts = __tlv$put(l_pdu, (unsigned short) a_bufsz, (tag), \
						TAG$K_LONGWORD, &l_u32, sizeof(l_u32))) ) return l_sts; }

	#define	$PUT_Q(tag, v)	{ l_i64 = (long long) (v); \
				if ( $IS_FAILURE(l_sts = __tlv$put(l_pdu, (unsigned short) a_bufsz, (tag), \
						TAG$K_QWORD, &l_i64, sizeof(l_i64))) ) return l_sts; }

	#define	$PUT_S(tag, v)	{ if ( $IS_FAILURE(l_sts = __tlv$put(l_pdu, (unsigned short) a_bufsz, (tag), \
						TAG$K_BBLOCK, (void *) (v), (unsigned) strlen(v) + 1)) ) return l_sts; }

	$PUT_L(JRN$K_ENTRY,	a_job->jb_entry);
	$PUT_L(JRN$K_STATE,	a_job->jb_state);
	$PUT_L(JRN$K_PRIO,	a_job->jb_prio);
	$PUT_L(JRN$K_RETAIN,	a_job->jb_retain);
	$PUT_L(JRN$K_RESTARTS,	a_job->jb_restarts);

	$PUT_L(JRN$K_PID,	a_job->jb_pid);
	$PUT_L(JRN$K_UID,	a_job->jb_uid);
	$PUT_L(JRN$K_GID,	a_job->jb_gid);
	$PUT_L(JRN$K_EXIT,	a_job->jb_exit);
	$PUT_L(JRN$K_SIGNAL,	a_job->jb_signal);

	$PUT_L(JRN$K_KEEPLOG,	a_job->jb_keeplog);
	$PUT_L(JRN$K_RESTART,	a_job->jb_restart);
	$PUT_L(JRN$K_LOGCWD,	a_job->jb_logcwd);
	$PUT_L(JRN$K_LOGCLIENT,	a_job->jb_logclient);

	$PUT_Q(JRN$K_QTIME,	a_job->jb_qtime);
	$PUT_Q(JRN$K_AFTER,	a_job->jb_after);
	$PUT_Q(JRN$K_STIME,	a_job->jb_stime);
	$PUT_Q(JRN$K_ETIME,	a_job->jb_etime);
	$PUT_Q(JRN$K_DEV,	a_job->jb_dev);
	$PUT_Q(JRN$K_INO,	a_job->jb_ino);
	$PUT_Q(JRN$K_MTIME,	a_job->jb_mtime);
	$PUT_Q(JRN$K_SIZE,	a_job->jb_size);
	$PUT_Q(JRN$K_FSID,	a_job->jb_fsid);

	$PUT_S(JRN$K_QUEUE,	a_job->jb_queue);
	$PUT_S(JRN$K_NAME,	a_job->jb_name);
	$PUT_S(JRN$K_CPUS,	a_job->jb_cpus);
	$PUT_S(JRN$K_SCRIPT,	a_job->jb_script);
	$PUT_S(JRN$K_LOGFILE,	a_job->jb_logfile);
	$PUT_S(JRN$K_CWD,	a_job->jb_cwd);
	$PUT_S(JRN$K_USER,	a_job->jb_user);

	/* An empty parameter is not written: its absence and its emptiness mean the same thing */
	for ( l_i = 0; l_i < BATCH$K_NPARM; l_i++ )
		{
		if ( a_job->jb_parm[l_i][0] )
			$PUT_S((unsigned) (JRN$K_PARM + l_i), a_job->jb_parm[l_i]);
		}

	#undef	$PUT_L
	#undef	$PUT_Q
	#undef	$PUT_S

	if ( $IS_FAILURE(l_sts = __tlv$hget(l_pdu, (unsigned char *) BATCHJRN$K_SIG, &l_ulen, &l_csr, &l_seq)) )
		return	l_sts;

	*a_len = l_ulen + (unsigned) sizeof(AVPROTO_HDR);

	return	BATCH$_NORMAL;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Decodes a record of the queue journal into a job descriptor.
**
**	A tag the record does not carry leaves its field at the default, and a tag this revision does
**	not know is simply never asked for. That is what lets a journal cross a revision in either
**	direction.
**
**  INPUT PARAMETERS:
**	a_buf	- the record, not NULL.
**	a_len	- its length.
**
**  OUTPUT PARAMETERS:
**	a_job	- receives the descriptor, not NULL. Zeroed first, so an absent tag yields a zero.
**
**  RETURNS:
**	BATCH$_NORMAL, or BATCH$_JRNCORRUPT where the record is not one of ours.
**--
*/
int	util$job_decode (
			const void *	a_buf,
			unsigned	a_len,
			BATCH$JOB *	a_job
			)
{
AVPROTO_PDU *	l_pdu = (AVPROTO_PDU *) a_buf;
unsigned int	l_u32;
long long	l_i64;
unsigned	l_ulen = 0, l_csr = 0, l_seq = 0, l_vsz, l_type;
int		l_i, l_ctx;

	memset(a_job, 0, sizeof(BATCH$JOB));

	if ( (a_len < sizeof(AVPROTO_HDR))
		|| ($IS_FAILURE(__tlv$hget(l_pdu, (unsigned char *) BATCHJRN$K_SIG, &l_ulen, &l_csr, &l_seq))) )
		return	BATCH$_JRNCORRUPT;

	#define	$GET_L(tag, f)	{ l_ctx = 0; l_vsz = sizeof(l_u32); \
				if ( $IS_SUCCESS(__tlv$get(l_pdu, a_len, &l_ctx, (tag), &l_type, &l_u32, &l_vsz)) ) \
					(f) = (typeof(f)) l_u32; }

	#define	$GET_Q(tag, f)	{ l_ctx = 0; l_vsz = sizeof(l_i64); \
				if ( $IS_SUCCESS(__tlv$get(l_pdu, a_len, &l_ctx, (tag), &l_type, &l_i64, &l_vsz)) ) \
					(f) = (typeof(f)) l_i64; }

	#define	$GET_S(tag, f)	{ l_ctx = 0; l_vsz = (unsigned) sizeof(f); \
				if ( $IS_SUCCESS(__tlv$get(l_pdu, a_len, &l_ctx, (tag), &l_type, (f), &l_vsz)) ) \
					(f)[sizeof(f) - 1] = '\0'; }

	$GET_L(JRN$K_ENTRY,	a_job->jb_entry);
	$GET_L(JRN$K_STATE,	a_job->jb_state);
	$GET_L(JRN$K_PRIO,	a_job->jb_prio);
	$GET_L(JRN$K_RETAIN,	a_job->jb_retain);
	$GET_L(JRN$K_RESTARTS,	a_job->jb_restarts);

	$GET_L(JRN$K_PID,	a_job->jb_pid);
	$GET_L(JRN$K_UID,	a_job->jb_uid);
	$GET_L(JRN$K_GID,	a_job->jb_gid);
	$GET_L(JRN$K_EXIT,	a_job->jb_exit);
	$GET_L(JRN$K_SIGNAL,	a_job->jb_signal);

	$GET_L(JRN$K_KEEPLOG,	a_job->jb_keeplog);
	$GET_L(JRN$K_RESTART,	a_job->jb_restart);
	$GET_L(JRN$K_LOGCWD,	a_job->jb_logcwd);
	$GET_L(JRN$K_LOGCLIENT,	a_job->jb_logclient);

	$GET_Q(JRN$K_QTIME,	a_job->jb_qtime);
	$GET_Q(JRN$K_AFTER,	a_job->jb_after);
	$GET_Q(JRN$K_STIME,	a_job->jb_stime);
	$GET_Q(JRN$K_ETIME,	a_job->jb_etime);
	$GET_Q(JRN$K_DEV,	a_job->jb_dev);
	$GET_Q(JRN$K_INO,	a_job->jb_ino);
	$GET_Q(JRN$K_MTIME,	a_job->jb_mtime);
	$GET_Q(JRN$K_SIZE,	a_job->jb_size);
	$GET_Q(JRN$K_FSID,	a_job->jb_fsid);

	$GET_S(JRN$K_QUEUE,	a_job->jb_queue);
	$GET_S(JRN$K_NAME,	a_job->jb_name);
	$GET_S(JRN$K_CPUS,	a_job->jb_cpus);
	$GET_S(JRN$K_SCRIPT,	a_job->jb_script);
	$GET_S(JRN$K_LOGFILE,	a_job->jb_logfile);
	$GET_S(JRN$K_CWD,	a_job->jb_cwd);
	$GET_S(JRN$K_USER,	a_job->jb_user);

	for ( l_i = 0; l_i < BATCH$K_NPARM; l_i++ )
		$GET_S((unsigned) (JRN$K_PARM + l_i), a_job->jb_parm[l_i]);

	#undef	$GET_L
	#undef	$GET_Q
	#undef	$GET_S

	return	BATCH$_NORMAL;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Writes exactly a_len bytes to a descriptor. Short writes and interruption of the call by a
**	signal are handled.
**
**  INPUT PARAMETERS:
**	a_fd	- descriptor of a socket or a file.
**	a_buf	- buffer to be transferred, not NULL.
**	a_len	- number of bytes to be written.
**
**  OUTPUT PARAMETERS:
**	None.
**
**  RETURNS:
**	BATCH$_NORMAL or BATCH$_IOERR.
**--
*/
int	util$write_exact (
		int		a_fd,
		const void *	a_buf,
		size_t		a_len
		)
{
const char *	l_p;
ssize_t		l_rc;
size_t		l_rest;

	l_p	= (const char *) a_buf;
	l_rest	= a_len;

	while ( l_rest )
		{
		l_rc = write(a_fd, l_p, l_rest);

		if ( (l_rc == -1) && (errno == EINTR) )
			continue;

		if ( !l_rc )
			return	$PUTMSG_FAO(BATCH$_ENDOFDATA, "write", (int) l_rc, a_len - l_rest, a_len);

		if ( l_rc < 0 )
			return	$PUTMSG_FAO(BATCH$_IOERR, "write", (int) l_rc, errno, strerror(errno));

		l_p	+= l_rc;
		l_rest	-= (size_t) l_rc;
		}

	return	BATCH$_NORMAL;
}

/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Renders the attributes a SET ENTRY request carries as a list a reader can check against what
**	was typed: "priority=200, hold, after=29-JUL-2026 03:00:00". The utility issues it in its
**	confirmation and the controller in its log, so that the two accounts of one change agree word
**	for word.
**
**	An attribute the request does not carry is not mentioned. A negated one is rendered with its
**	negation - "nohold", "noafter" - since what was done to it is the point of the line.
**
**  INPUT PARAMETERS:
**	a_req	- the request, not NULL.
**	a_bufsz	- size of the output buffer in bytes.
**
**  OUTPUT PARAMETERS:
**	a_buf	- buffer receiving the result, not NULL. A request carrying nothing yields "nothing".
**
**  RETURNS:
**	BATCH$_NORMAL or BATCH$_BUFOVF.
**--
*/
int	util$fmt_setent (
		const BATCH$SETENT *	a_req,
		char *			a_buf,
		size_t			a_bufsz
		)
{
char		l_work [BATCH$K_NAMESZ + BATCH$K_CPUSZ + 160], l_part [BATCH$K_NAMESZ + 32], l_tim [UTIL$K_TIMSZ];
const char *	l_kwd;
size_t		l_len = 0;
int		l_i;

	l_work[0] = '\0';

	for ( l_i = 0; l_i < 8; l_i++ )
		{
		if ( !(a_req->se_mask & (1U << l_i)) )
			continue;

		switch ( 1U << l_i )
			{
			case	SETENT$M_PRIORITY:
				util$mkfspec(l_part, sizeof(l_part), "priority=!UL",
						(const fao_prm_t []) { (fao_prm_t) a_req->se_prio }, 1);
				break;

			case	SETENT$M_AFTER:
				if ( !a_req->se_after )
					{
					util$mkfspec(l_part, sizeof(l_part), "noafter", NULL, 0);
					break;
					}

				util$fmt_time(a_req->se_after, l_tim, sizeof(l_tim));
				util$mkfspec(l_part, sizeof(l_part), "after=!AZ", (const fao_prm_t []) { (fao_prm_t) l_tim }, 1);
				break;

			case	SETENT$M_HOLD:
				util$mkfspec(l_part, sizeof(l_part), "!AZ",
						(const fao_prm_t []) { (fao_prm_t) ((a_req->se_hold) ? "hold" : "nohold") }, 1);
				break;

			case	SETENT$M_NAME:
				util$mkfspec(l_part, sizeof(l_part), "name=!AZ", (const fao_prm_t []) { (fao_prm_t) a_req->se_name }, 1);
				break;

			case	SETENT$M_RETAIN:
				l_kwd = (a_req->se_retain == RETAIN$K_ALWAYS) ? "always"
					: (a_req->se_retain == RETAIN$K_NEVER) ? "never" : "error";
				util$mkfspec(l_part, sizeof(l_part), "retain=!AZ", (const fao_prm_t []) { (fao_prm_t) l_kwd }, 1);
				break;

			case	SETENT$M_KEEP:
				util$mkfspec(l_part, sizeof(l_part), "!AZ",
						(const fao_prm_t []) { (fao_prm_t) ((a_req->se_keep) ? "keep" : "nokeep") }, 1);
				break;

			case	SETENT$M_RESTART:
				util$mkfspec(l_part, sizeof(l_part), "!AZ",
						(const fao_prm_t []) { (fao_prm_t) ((a_req->se_restart) ? "restart" : "norestart") }, 1);
				break;

			case	SETENT$M_CPU:
				util$mkfspec(l_part, sizeof(l_part), "cpu=!AZ", (const fao_prm_t []) { (fao_prm_t) a_req->se_cpus }, 1);
				break;

			default:
				continue;
			}

		if ( (l_len + strlen(l_part) + 2) >= sizeof(l_work) )
			return	BATCH$_BUFOVF;

		if ( l_len )
			{
			memcpy(l_work + l_len, ", ", 3);
			l_len += 2;
			}

		memcpy(l_work + l_len, l_part, strlen(l_part) + 1);
		l_len += strlen(l_part);
		}

	if ( !l_len )
		{
		memcpy(l_work, "nothing", 8);
		l_len = 7;
		}

	if ( l_len >= a_bufsz )
		return	BATCH$_BUFOVF;

	memcpy(a_buf, l_work, l_len + 1);

	return	BATCH$_NORMAL;
}
