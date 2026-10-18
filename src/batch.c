/*
**++
**  MODULE:		BATCH.C
**  IDENT:		X-02-02
**  REV:		02.02.00
**
**  FACILITY:		BATCH
**
**  ABSTRACT:		Command line utility of the batch execution subsystem. It parses a command and
**			its qualifiers in the DCL convention, builds a request and passes it to the job
**			controller BATCHD over an AF_UNIX socket, then displays the result.
**
**			Command format:
**
**			  batch submit <script> [/NAME=name] [/QUEUE=queue] [/AFTER=time] [/HOLD]
**					[/PRIORITY=n] [/PARAMETERS=(p1,...,p8)] [/LOG_FILE=file]
**					[/KEEP] [/RESTART] [/CPU=list] [/RETAIN=ALWAYS|ERROR|NEVER]
**			  batch show   [entry] [/ALL] [/FULL] [/EVERYONE] [/PAGE] [/QUEUE=queue]
**			  batch delete <entry>
**			  batch release <entry>
**			  batch sync    <entry> [/TIMEOUT=seconds]
**			  batch set entry <entry-list> [/PRIORITY=n] [/AFTER=time | /NOAFTER] [/HOLD | /NOHOLD]
**					[/NAME=name] [/RETAIN=kw] [/[NO]KEEP] [/[NO]RESTART] [/CPU=list]
**
**  AUTHOR:		StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
**
**  CREATION DATE:	28-JUL-2026
**
**  MODIFICATION HISTORY:
**
**	28-JUL-2026	RRL	X-01-01	Initial revision.
**
**	28-JUL-2026	RRL	X-01-02	A qualifier table was introduced together with separate recognition of a
**				qualifier and a file specification: in Unix both begin with a slash.
**
**	28-JUL-2026	RRL	X-01-03	Conformance to the coding rules: every routine returns a status, values
**				are delivered through output parameters, braces removed from single
**				statements under if, type CLI_CTX_t renamed to BATCH$CLICTX.
**
**	28-JUL-2026	RRL	X-01-04	Routines return condition values of the facility from BATCHMSG.H. A
**				condition produced by the job controller is received in the mh_sts field of
**				the reply and issued by the utility as its own, without substituted text.
**
**	28-JUL-2026	RRL	X-01-05	Comments and message texts converted to English.
**
**	28-JUL-2026	RRL	X-01-06	The qualifier table is bounded by the enumerated count QUAL$K_MAXNR
**				instead of a terminating element, and BATCH$QUAL_LOOKUP is bounded by that
**				count as well: the previous traversal read one element past the table.
**
**	28-JUL-2026	RRL	X-01-07	The command line is parsed by the CLI services of UTILITY$ROUTINES.
**				Routines BATCH$QUAL_LOOKUP and BATCH$PARSE_QUAL were withdrawn, matching
**				and validation of values now being performed by the library; s_BATCH$IS_QUAL
**				remains, since the vector has to be rebuilt with the parameters ahead of
**				the qualifiers, and the shape of an argument does not tell a qualifier from
**				a file specification.
**
**	28-JUL-2026	RRL	X-01-08	Static routines were given the prefix s_ as the coding rules require.
**
**	28-JUL-2026	RRL	X-01-09	The result of every C RTL routine is captured at the point of the call and
**				reported together with errno.
**
**	28-JUL-2026	RRL	X-01-10	Formatting converted to the FAO engine of STARLET, every line of output
**				now passing through UTIL$PUT. The tabular format of SHOW is expressed with
**				the fixed-width field directive !n<...!>, and the dependence on <stdio.h>
**				was withdrawn.
**
**	29-JUL-2026	RRL	X-01-11	Qualifier /RESTART added. It shares its first two letters with /RETAIN,
**				so an abbreviation of either takes three.
**
**	29-JUL-2026	RRL	X-01-12	Qualifier /CPU added. Its value is verified here, where the submitter
**				can be told of a mistake at once.
**
**	30-JUL-2026	RRL	X-01-13	The CLI services are called by their names in the revision now vendored,
**				which carry the __cli$ prefix.
**
**	 1-AUG-2026	RRL	X-01-14	The HELP command was added. It answers through the reading API of the
**				HELP facility rather than by spawning its image, and is intercepted before
**				the command line is parsed: its parameters are topic names, spelled however
**				the documentation spells them, which the CLI services would refuse.
**
**	 1-AUG-2026	RRL	X-01-15	An abbreviated verb reached the wrong command. The CLI services accept an
**				unambiguous leading portion, and the dispatch resolved the verb a second
**				time by comparing the word in full, so every abbreviation fell through the
**				chain to its last branch: BATCH DEL 1 released entry 1 rather than deleting
**				it. The verb is now taken from the parse context by the address of its
**				descriptor, and there is no default branch to fall into.
**
**	 1-AUG-2026	RRL	X-01-16	A condition returned by the controller is reported by the mnemonic of its
**				message rather than by its value alone. The arguments of a message do not
**				cross the connection, so the text cannot be formatted here, but the part
**				before the first comma carries no conversion and names the condition.
**
**	 1-AUG-2026	RRL	X-01-17	The submission names the state the job was placed in and, where it is held
**				until a time, that time. A delta specification is easy to write and hard to
**				read back, the day field being told from the hour field by a hyphen alone,
**				and until now the resolved time was not shown anywhere but SHOW /FULL.
**
**	 2-AUG-2026	RRL	X-01-18	SHOW honours an entry number. It was accepted from the command line and
**				then dropped without a word, so SHOW 7 displayed the whole queue and the
**				enquirer had no way of telling that his request had been disregarded.
**
**	 2-AUG-2026	RRL	X-01-19	The ANALYZE command was added. It reads the journal directly rather than
**				through the controller, so it serves where the controller will not start,
**				and it writes nothing: what to do about a damaged journal is a decision for
**				whoever ran the command.
**
**	 2-AUG-2026	RRL	X-01-20	ANALYZE names the tags of a record that this revision does not assign and
**				shows their octets. Their presence is no fault - passing over the unknown is
**				what lets a journal cross a revision - but a journal written by a later
**				revision and one quietly corrupted look alike until the tags are named.
**
**	 2-AUG-2026	RRL	X-01-21	The summary listing names the time a held job is to start, in the column
**				that reports what became of a job which has run - such a job having no
**				status to report, the column held a dash where the one fact worth knowing
**				about it belonged, and /FULL was needed for it.
**
**	 2-AUG-2026	RRL	X-01-22	The confirmations of SUBMIT, DELETE and RELEASE are issued as conditions.
**				Everything this subsystem says of itself carries the facility, the severity
**				and a mnemonic, and these were the places where it did not: an operation
**				reported its success in plain words, which no other message did.
**	 2-AUG-2026	RRL	X-01-23	The /PAGE qualifier of SHOW. The paging is that of the HELP facility, whose
**				output layer this subsystem already links, and it is entered at UTIL$PUT
**				rather than at each of the thirty places that write a line of a display.
**	 2-AUG-2026	RRL	X-01-24	The SYNC command waits for a job and carries its outcome in the exit
**				status, for a script that has work to do after a job. The waiting is done by
**				asking after the entry rather than by holding a connection open: a connection
**				held would have to be excused the timeout that guards against a client which
**				has stopped talking, and that guard is worth more than a second of delay.
**
**	 3-AUG-2026	RRL	X-01-25	SHOW/MANAGER reports the state of the job controller, which a display of
**				the queue cannot: a queue that stands still looks the same whether the
**				controller is busy, idle or gone.
**
**				The absence of jobs is issued as a condition rather than written as a line
**				of plain words. The display proper stays plain: that is data.
**
**				Message numbers 50 and 51 were held by two conditions each, and 52 to 54 by
**				two apiece as well. A message is looked up by its number, so SYNC handed its
**				arguments to the text of BATCH$_OPENIN and took the process down with it.
**
**	 4-AUG-2026	RRL	X-01-26	DELETE and RELEASE take a list of entries, written as DCL writes one.
**				Before this the first number was taken and the rest passed over without a
**				word - and the command reported success for the one entry it did dispose
**				of, which is the worst of the possible answers. /ENTRY takes a list too and
**				is no longer declared a number.
**
**				The usage text named nine conversions and was given eight arguments: a line
**				of it had been duplicated and then removed without the count being amended.
**
**	 5-AUG-2026	RRL	X-01-27	A qualifier is /name or --name. CLI_ROUTINES X-00-14 withdrew the single
**				hyphen, and the classification here had to agree: an argument taken for a
**				qualifier here and refused as a name there would have produced a diagnostic
**				about something the user did not write.
**
**	 5-AUG-2026	RRL	X-01-28	SHOW/QUEUES displays the table of queues, and the QUEUE verb pauses one or
**				lets it go on again. The table travels with the state of the controller in
**				one reply, so the two cannot disagree; pausing is granted to the privileged
**				user alone, ownership of a job saying nothing of the queue it waits in.
**
**	20-AUG-2026	RRL	X-01-29	The PURGE command removes the entries of jobs that have finished, and
**				S_BATCH$CONFIRM asks before an irrevocable act where /CONFIRM asks for it.
**				A refusal is told apart by its identification: BATCH$_NOTCONF is of
**				informational severity, and an informational status carries the low bit set,
**				which $IS_FAILURE reads as success.
**
**	21-AUG-2026	RRL	X-01-30	The dump of ANALYZE tells how a job ended, and what a held one waits
**				for. A retained entry is retained because the job failed, and how it failed
**				was the one thing the dump did not say - it named the state and left the
**				reader to guess the rest. The queue and the log are named as well.
**
**	21-AUG-2026	RRL	X-01-31	Every label of the dump is followed by a colon and every value stands
**				apart from it. A dump is read by eye and by script alike, and a line whose
**				parts are separated only by spaces obliges both to count words.
**
**	28-AUG-2026	RRL	X-01-32	PURGE, SHOW/MANAGER and SYNC look at the condition the controller
**				returned before they look at the count. Without it a refused request was
**				reported as a purge that removed nothing: the count of a refusal is zero
**				because nothing was counted, and the refusal itself was never seen. It shows
**				itself where an old controller is still running under a new utility - the
**				function code is unknown to it, it answers BATCH$_BADFUNC, and the queue
**				stays as it was while the command reports success.
**
**	28-AUG-2026	RRL	X-01-33	Diagnostics are issued by $PUTMSG_FAO. The message table is written for
**				the FAO engine, and the envelope %FAC-S-IDENT is supplied by the library
**				rather than carried in every text - a text that carried its own was framed
**				twice. The parameters are unchanged at the point of call: $PUTMSG_FAO casts
**				each to FAO_PRM_T itself and derives their count from the list.
**
**	 8-SEP-2026	RRL	X-01-34	The SET ENTRY command alters the attributes of a job that is in the queue -
**				its priority, its start time, whether it is held, its name and the rest of
**				what SUBMIT settles - without the entry being deleted and the job submitted
**				anew, which loses the entry number and the place in the order.
**
**				It is the first two-word verb of this utility. The CLI services carry a
**				sub-verb table of their own, so ENTRY is a verb under SET rather than a
**				parameter of it, and the classification of arguments here puts the word
**				after SET back where those services expect it. /HOLD, /KEEP, /RESTART and
**				/AFTER became negatable for it, and are read by CLI$PRESENT so that /NOHOLD
**				is told from /HOLD; SUBMIT disregards the negated forms as it does every
**				qualifier that is not its own.
**
**	 7-OCT-2026	RRL	X-01-35	The HELP command looks for its topics under the facility key BATCH, where
**				the library has been rooted since X-01-09 of the kit, and falls back to the
**				top of the library: HELP SUBMIT had answered NOSUCHTOPIC everywhere the new
**				library was installed, and the smoke test had passed only where an older
**				library was still lying in the place it is looked for first.
**
**	18-OCT-2026	RRL	X-02-02	Revision raised to X-02-02 together with the product; no change of its own.
**--
*/

#include	<ctype.h>
#include	<errno.h>
#include	<libgen.h>
#include	<limits.h>
#include	<pwd.h>
#include	<stdlib.h>
#include	<string.h>
#include	<unistd.h>
#include	<sys/socket.h>
#include	<sys/stat.h>
#include	<sys/types.h>
#include	<sys/un.h>

#include	"batchdef.h"
#include	<fcntl.h>

#include	"batchipc.h"
#include	"batchjrn.h"

/*
**  The HELP facility, for the HELP command. Its header carries an identification of its own, so it
**  is drawn in before ours is established rather than after.
*/
#include	"helpdef.h"

#undef	__MODULE__
#undef	__IDENT__
#undef	__REV__

#include	"cli_routines.h"

#undef	__MODULE__
#undef	__IDENT__
#undef	__REV__

#define	__MODULE__	"BATCH"
#define	__IDENT__	"X-02-02"
#define	__REV__		"02.02.00"

#define	BATCH$K_MINABBR	2				/* Shortest admissible abbreviation of a qualifier 			*/

/*
**  Command line parsing context: the values of every recognized qualifier accumulate here.
*/
typedef struct batch_clictx_t {
	unsigned int	cc_showflg;			/* Combination of the SHOW$M_* indicators 				*/
	unsigned int	cc_entry;
	char		cc_entrystr [BATCH$K_CPUSZ];		/* /ENTRY as written: a list, not a number		*/

	int		cc_hold;			/* The /HOLD qualifier was given 					*/
	int		cc_nohold;			/* The /NOHOLD qualifier was given 					*/
	int		cc_keep;
	int		cc_nokeep;
	int		cc_norestart;
	int		cc_noafter;			/* /NOAFTER: the time is to be cleared					*/
	int		cc_prioset;			/* /PRIORITY was given, whatever its value 				*/
	int		cc_retainset;			/* /RETAIN was given 							*/
	int		cc_dump;
	char		cc_user [BATCH$K_USERSZ];
	int		cc_page;
	int		cc_timeout;
	int		cc_manager;
	int		cc_queues;
	int		cc_pause;
	int		cc_resume;
	int		cc_confirm;
	int		cc_noconfirm;
	int		cc_restart;			/* The /KEEP qualifier was given 					*/
	int		cc_prio;			/* Value of /PRIORITY, zero denotes the default 			*/
	int		cc_retain;			/* Value of /RETAIN, one of RETAIN$K_* 					*/

	long long	cc_after;			/* Value of /AFTER in seconds of the epoch 				*/

	char		cc_name [BATCH$K_NAMESZ];	/* Value of /NAME 							*/
	char		cc_cpus [BATCH$K_CPUSZ];
	char		cc_queue [BATCH$K_QUESZ];	/* Value of /QUEUE 							*/
	char		cc_logfile [BATCH$K_PATHSZ];	/* Value of /LOG_FILE 							*/
	char		cc_parm [BATCH$K_NPARM][BATCH$K_PARMSZ]; /* Values of /PARAMETERS 					*/
} BATCH$CLICTX;

/*
**  Command line definition in the CLI convention of UTILITY$ROUTINES. One qualifier table serves
**  every verb: a qualifier that makes no sense for a given command is accepted and disregarded,
**  which is what the previous parser did as well.
**
**  The order of the rows corresponds to the QUAL$K_* values, and CLI$GET_VALUE identifies a
**  qualifier by the address of its descriptor, so the table must not be duplicated per verb.
**
**  /HOLD, /KEEP, /RESTART and /AFTER are negatable, for SET ENTRY, which has to be able to take an
**  attribute away as well as give it. CLI$GET_VALUE reports a negated qualifier as present like any
**  other, so those four are read by CLI$PRESENT, which tells the two forms apart.
*/
enum	{
	QUAL$K_ALL = 0,
	QUAL$K_FULL,
	QUAL$K_EVERYONE,
	QUAL$K_HOLD,
	QUAL$K_KEEP,
	QUAL$K_NAME,
	QUAL$K_QUEUE,
	QUAL$K_LOG_FILE,
	QUAL$K_PARAMETERS,
	QUAL$K_PRIORITY,
	QUAL$K_AFTER,
	QUAL$K_RETAIN,
	QUAL$K_RESTART,
	QUAL$K_CPU,
	QUAL$K_ENTRY,
	QUAL$K_DUMP,
	QUAL$K_USER,
	QUAL$K_PAGE,
	QUAL$K_TIMEOUT,
	QUAL$K_MANAGER,
	QUAL$K_QUEUES,
	QUAL$K_PAUSE,
	QUAL$K_RESUME,
	QUAL$K_CONFIRM,
	QUAL$K_NOCONFIRM,

	QUAL$K_MAXNR
	};

/*
**  Keywords of the /RETAIN qualifier. The CLI services validate the value against this list and
**  reject anything else, so the module no longer compares the keyword itself.
*/
static	CLI_KEYWORD	s_retkwd [] =
	{
	{ {$ASCINI("ALWAYS")},	RETAIN$K_ALWAYS	},
	{ {$ASCINI("ERROR")},	RETAIN$K_ERROR	},
	{ {$ASCINI("NEVER")},	RETAIN$K_NEVER	},
	{ {0},			0		}
	};

static	CLI_PQDESC	s_quals [QUAL$K_MAXNR + 1] =
	{
	{ {$ASCINI("ALL")},		CLI$K_OPT,	CLI$K_QUAL, 0, {0}, NULL },
	{ {$ASCINI("FULL")},		CLI$K_OPT,	CLI$K_QUAL, 0, {0}, NULL },
	{ {$ASCINI("EVERYONE")},	CLI$K_OPT,	CLI$K_QUAL, 0, {0}, NULL },
	{ {$ASCINI("HOLD")},		CLI$K_OPT,	CLI$K_QUAL, CLI$M_NEGATABLE, {0}, NULL },
	{ {$ASCINI("KEEP")},		CLI$K_OPT,	CLI$K_QUAL, CLI$M_NEGATABLE, {0}, NULL },
	{ {$ASCINI("NAME")},		CLI$K_QSTRING,	CLI$K_QUAL, 0, {0}, NULL },
	{ {$ASCINI("QUEUE")},		CLI$K_QSTRING,	CLI$K_QUAL, 0, {0}, NULL },
	{ {$ASCINI("LOG_FILE")},	CLI$K_QSTRING,	CLI$K_QUAL, 0, {0}, NULL },
	{ {$ASCINI("PARAMETERS")},	CLI$K_QSTRING,	CLI$K_QUAL, 0, {0}, NULL },
	{ {$ASCINI("PRIORITY")},	CLI$K_NUM,	CLI$K_QUAL, 0, {0}, NULL },
	{ {$ASCINI("AFTER")},		CLI$K_QSTRING,	CLI$K_QUAL, CLI$M_NEGATABLE, {0}, NULL },
	{ {$ASCINI("RETAIN")},		CLI$K_KWD,	CLI$K_QUAL, 0, {0}, s_retkwd },
	{ {$ASCINI("RESTART")},		CLI$K_OPT,	CLI$K_QUAL, CLI$M_NEGATABLE, {0}, NULL },
	{ {$ASCINI("CPU")},		CLI$K_QSTRING,	CLI$K_QUAL, 0, {0}, NULL },
	{ {$ASCINI("ENTRY")},		CLI$K_QSTRING,	CLI$K_QUAL, 0, {0}, NULL },
	{ {$ASCINI("DUMP")},		CLI$K_OPT,	CLI$K_QUAL, 0, {0}, NULL },
	{ {$ASCINI("USER")},		CLI$K_QSTRING,	CLI$K_QUAL, 0, {0}, NULL },
	{ {$ASCINI("PAGE")},		CLI$K_OPT,	CLI$K_QUAL, 0, {0}, NULL },
	{ {$ASCINI("TIMEOUT")},		CLI$K_NUM,	CLI$K_QUAL, 0, {0}, NULL },
	{ {$ASCINI("MANAGER")},		CLI$K_OPT,	CLI$K_QUAL, 0, {0}, NULL },
	{ {$ASCINI("QUEUES")},		CLI$K_OPT,	CLI$K_QUAL, 0, {0}, NULL },
	{ {$ASCINI("PAUSE")},		CLI$K_OPT,	CLI$K_QUAL, 0, {0}, NULL },
	{ {$ASCINI("RESUME")},		CLI$K_OPT,	CLI$K_QUAL, 0, {0}, NULL },
	{ {$ASCINI("CONFIRM")},		CLI$K_OPT,	CLI$K_QUAL, 0, {0}, NULL },
	{ {$ASCINI("NOCONFIRM")},	CLI$K_OPT,	CLI$K_QUAL, 0, {0}, NULL },
	{ {0},				0,		0,	    0, {0}, NULL }
	};

/*
**  The verbs. None of them declares a parameter, deliberately: the CLI services take a declared
**  parameter from the head of the vector unconditionally, without regard to whether the argument
**  standing there is a qualifier. With a parameter declared, "submit /NAME=x" would hand /NAME=x
**  to P1 and "delete /ENTRY=n" would hand it /ENTRY=n. The single parameter each verb accepts is
**  therefore taken by this module from the arguments it has already classified.
*/
/*
**  The verbs, in the order of the table below. The dispatch takes the verb from the parse context
**  by the address of its descriptor, not by comparing the word the user wrote: the CLI services
**  accept an unambiguous abbreviation, so BATCH SUBM is SUBMIT to them, and a second resolution of
**  our own would have to repeat their rule to agree with them. It did not, and every abbreviation
**  fell through to the last branch of the chain - BATCH DEL 1 released the entry it was meant to
**  delete.
*/
enum	{
	VERB$K_SUBMIT = 0,
	VERB$K_SHOW,
	VERB$K_DELETE,
	VERB$K_RELEASE,
	VERB$K_ANALYZE,
	VERB$K_SYNC,
	VERB$K_QUEUE,
	VERB$K_PURGE,
	VERB$K_SET,

	VERB$K_MAXNR
	};

/*
**  What SET may be asked to set. The CLI services take the word after SET from the sub-verb table
**  of that verb, so ENTRY is a verb in its own right there, with the qualifier table of the command,
**  and is told from a parameter by the dispatch below in the same way the verb is: by the address of
**  its descriptor.
*/
enum	{
	SETWHAT$K_ENTRY = 0,

	SETWHAT$K_MAXNR
	};

static	CLI_VERB	s_setwhat [] =
	{
	{ {$ASCINI("ENTRY")},	NULL, NULL, s_quals, NULL, NULL },
	{ {0},			NULL, NULL, NULL,    NULL, NULL }
	};

static	CLI_VERB	s_verbs [] =
	{
	{ {$ASCINI("SUBMIT")},	NULL, NULL, s_quals, NULL, NULL },
	{ {$ASCINI("SHOW")},	NULL, NULL, s_quals, NULL, NULL },
	{ {$ASCINI("DELETE")},	NULL, NULL, s_quals, NULL, NULL },
	{ {$ASCINI("RELEASE")},	NULL, NULL, s_quals, NULL, NULL },
	{ {$ASCINI("ANALYZE")},	NULL, NULL, s_quals, NULL, NULL },
	{ {$ASCINI("SYNC")},	NULL, NULL, s_quals, NULL, NULL },
	{ {$ASCINI("QUEUE")},	NULL, NULL, s_quals, NULL, NULL },
	{ {$ASCINI("PURGE")},	NULL, NULL, s_quals, NULL, NULL },
	{ {$ASCINI("SET")},	s_setwhat, NULL, NULL, NULL, NULL },
	{ {0},			NULL, NULL, NULL,    NULL, NULL }
	};

static	char	g_root [BATCH$K_PATHSZ] = BATCH$K_DEFROOT;

/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Issues the product identification and a summary of the command format.
**
**  INPUT PARAMETERS:
**	a_prog	- image name taken from the argument vector, not NULL.
**
**  OUTPUT PARAMETERS:
**	None.
**
**  RETURNS:
**	BATCH$_NORMAL.
**--
*/
static	int	s_batch$usage (
			const char *	a_prog
			)
{
	util$put("!AZ !AZ (!AZ), build !AZ\n\n"
		"Usage:\n"
		"  !AZ submit <script.sh> [/NAME=name] [/QUEUE=queue] [/AFTER=time] [/HOLD]\n"
		"                        [/PRIORITY=n] [/PARAMETERS=(p1,...)] [/LOG_FILE=file]\n"
		"                        [/KEEP] [/RESTART] [/CPU=list] [/USER=name]\n"
		"                        [/RETAIN=ALWAYS|ERROR|NEVER]\n"
		"  !AZ show [entry] [/ALL] [/FULL] [/EVERYONE] [/PAGE] [/QUEUE=queue]\n"
		"  !AZ delete <entry-list>\n"
		"  !AZ release <entry-list>\n"
		"  !AZ sync <entry> [/TIMEOUT=seconds]\n"
		"  !AZ queue <name> /PAUSE | /RESUME\n"
		"  !AZ purge [/QUEUE=queue] [/CONFIRM]\n"
		"  !AZ set entry <entry-list> [/PRIORITY=n] [/AFTER=time | /NOAFTER] [/HOLD | /NOHOLD]\n"
		"                           [/NAME=name] [/RETAIN=kw] [/[NO]KEEP] [/[NO]RESTART] [/CPU=list]\n"
		"\n"
		"Format of /AFTER: \"+[days-]hh[:mm[:ss]]\", dd-MMM-yyyy[:hh:mm:ss], NOW, TODAY or TOMORROW.\n"
		"The subsystem root directory is taken from the environment variable BATCH_ROOT.\n",
		(const fao_prm_t []) { (fao_prm_t) BATCH$K_FACILITY, (fao_prm_t) BATCH$K_VERSION,
					(fao_prm_t) BATCH$K_IDENT, (fao_prm_t) BATCH$K_BUILDID,
					(fao_prm_t) a_prog, (fao_prm_t) a_prog,
					(fao_prm_t) a_prog, (fao_prm_t) a_prog,
					(fao_prm_t) a_prog, (fao_prm_t) a_prog,
					(fao_prm_t) a_prog, (fao_prm_t) a_prog }, 12);

	return	BATCH$_NORMAL;
}

static	int	s_batch$condname (unsigned int a_sts, char *a_buf, size_t a_bufsz);

/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Determines whether a command line argument is meant as a qualifier rather than as a file
**	specification. In Unix both begin with a slash, so the distinction is drawn by what follows:
**	an argument is meant as a qualifier when the text up to the first equals sign carries no slash
**	of its own.
**
**	Whether the name so found is a qualifier this command knows is not asked here, and
**	deliberately. An argument meant as a qualifier is handed to the CLI services, which match it
**	against the table and name a mistyped one in their diagnostic; were it kept back it would
**	arrive at the command as a second file specification, and the message would speak of a
**	parameter where the user had written a qualifier.
**
**  INPUT PARAMETERS:
**	a_arg	- command line argument, not NULL.
**
**  OUTPUT PARAMETERS:
**	a_isqual - receives one for an argument meant as a qualifier, zero for anything else, not NULL.
**
**  RETURNS:
**	BATCH$_NORMAL.
**--
*/
static	int	s_batch$is_qual (
			const char *	a_arg,
			int *		a_isqual
			)
{
const char *	l_end, *l_name;
size_t		l_len;

	*a_isqual = 0;

	/*
	**  A qualifier is /name or --name, and nothing else. CLI_ROUTINES X-00-14 withdrew the single
	**  hyphen: a lone -name is an ordinary value now, and this must agree with it - were an
	**  argument classified here as a qualifier and refused there as a name, the diagnostic would
	**  speak of something the user did not write.
	*/
	if ( *a_arg == '/' )
		l_name = a_arg + 1;
	else if ( (a_arg[0] == '-') && (a_arg[1] == '-') )
		l_name = a_arg + 2;
	else	return	BATCH$_NORMAL;

	l_end	= strchr(l_name, '=');
	l_len	= (l_end) ? (size_t) (l_end - l_name) : strlen(l_name);

	/* A slash within the name marks a file specification beyond doubt */
	if ( (l_len < BATCH$K_MINABBR) || (memchr(l_name, '/', l_len)) )
		return	BATCH$_NORMAL;

	*a_isqual = 1;

	return	BATCH$_NORMAL;
}

/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Parses the value of the /PARAMETERS qualifier. Both the form "(p1,p2,p3)" and the form
**	"p1,p2,p3" are accepted. The values are placed in the cc_parm fields in order of appearance.
**
**  INPUT PARAMETERS:
**	a_val	- value of the qualifier, not NULL.
**
**  OUTPUT PARAMETERS:
**	a_ctx	- parsing context; the cc_parm fields are filled in, not NULL.
**
**  RETURNS:
**	BATCH$_NORMAL, or BATCH$_MAXPARM if more than BATCH$K_NPARM parameters were given: the excess
**	are discarded but the command is not rejected.
**--
*/
static	int	s_batch$parse_parms (
			const char *	a_val,
			BATCH$CLICTX *	a_ctx
			)
{
char	l_work [BATCH$K_NPARM * BATCH$K_PARMSZ], *l_p, *l_tok, *l_sav;
int	l_n;

	util$mkfspec(l_work, sizeof(l_work), "!AZ", (const fao_prm_t []) { (fao_prm_t) a_val }, 1);
	l_p = l_work;

	/* Strip the enclosing parentheses if they were given */
	if ( *l_p == '(' )
		{
		l_p++;
		l_tok = strrchr(l_p, ')');

		if ( l_tok )
			*l_tok = '\0';
		}

	l_n	= 0;
	l_sav	= NULL;

	for ( l_tok = strtok_r(l_p, ",", &l_sav); l_tok; l_tok = strtok_r(NULL, ",", &l_sav) )
		{
		if ( l_n >= BATCH$K_NPARM )
			return	$PUTMSG_FAO(BATCH$_MAXPARM, BATCH$K_NPARM);

		util$mkfspec(a_ctx->cc_parm[l_n], BATCH$K_PARMSZ, "!AZ", (const fao_prm_t []) { (fao_prm_t) l_tok }, 1);
		l_n++;
		}

	return	BATCH$_NORMAL;
}

/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Draws the values of the qualifiers out of the parsed command line and places them in the
**	context of this module.
**
**	Recognition, abbreviation, ambiguity and validation of a value against its declared type are
**	all performed by the CLI services beforehand, so what remains here is the handling peculiar
**	to this command: splitting the parameter list, resolving the log file specification and
**	converting the time specification.
**
**  INPUT PARAMETERS:
**	a_clictx - the parsed command line, not NULL.
**
**  OUTPUT PARAMETERS:
**	a_ctx	- context of this module, not NULL.
**
**  RETURNS:
**	BATCH$_NORMAL, BATCH$_MAXPARM, BATCH$_INVTIME, BATCH$_NODIR, BATCH$_CWDERR or BATCH$_BUFOVF.
**--
*/
static	int	s_batch$get_quals (
			CLI_CTX *	a_clictx,
			BATCH$CLICTX *	a_ctx
			)
{
ASC		l_val;
cpu_set_t	l_cpus;
long long	l_tim;
char		l_real [PATH_MAX], l_dir [BATCH$K_PATHSZ], *l_p;
unsigned	l_state;
int		l_sts, l_ncpu;

	if ( $IS_SUCCESS(__cli$get_value(a_clictx, &s_quals[QUAL$K_ALL], NULL)) )
		a_ctx->cc_showflg |= SHOW$M_ALL;

	if ( $IS_SUCCESS(__cli$get_value(a_clictx, &s_quals[QUAL$K_FULL], NULL)) )
		a_ctx->cc_showflg |= SHOW$M_FULL;

	if ( $IS_SUCCESS(__cli$get_value(a_clictx, &s_quals[QUAL$K_EVERYONE], NULL)) )
		a_ctx->cc_showflg |= SHOW$M_EVERYONE;

	/*
	**  The negatable qualifiers. CLI$GET_VALUE says only that the qualifier was written, and
	**  /NOHOLD written is /HOLD taken away, not given; CLI$PRESENT says which of the two it was,
	**  and the last occurrence on the line decides, as it does in DCL.
	*/
	if ( $IS_SUCCESS(__cli$present(a_clictx, &s_quals[QUAL$K_HOLD], &l_state)) )
		{
		if ( l_state & CLI$M_NEGATED )
			a_ctx->cc_nohold = 1;
		else	a_ctx->cc_hold = 1;
		}

	if ( $IS_SUCCESS(__cli$present(a_clictx, &s_quals[QUAL$K_KEEP], &l_state)) )
		{
		if ( l_state & CLI$M_NEGATED )
			a_ctx->cc_nokeep = 1;
		else	a_ctx->cc_keep = 1;
		}

	if ( $IS_SUCCESS(__cli$present(a_clictx, &s_quals[QUAL$K_RESTART], &l_state)) )
		{
		if ( l_state & CLI$M_NEGATED )
			a_ctx->cc_norestart = 1;
		else	a_ctx->cc_restart = 1;
		}

	if ( $IS_SUCCESS(__cli$present(a_clictx, &s_quals[QUAL$K_AFTER], &l_state)) && (l_state & CLI$M_NEGATED) )
		a_ctx->cc_noafter = 1;

	if ( $IS_SUCCESS(__cli$get_value(a_clictx, &s_quals[QUAL$K_DUMP], NULL)) )
		a_ctx->cc_dump = 1;

	if ( $IS_SUCCESS(__cli$get_value(a_clictx, &s_quals[QUAL$K_USER], &l_val)) )
		__util$asc2str(&l_val, a_ctx->cc_user);

	if ( $IS_SUCCESS(__cli$get_value(a_clictx, &s_quals[QUAL$K_PAGE], NULL)) )
		a_ctx->cc_page = 1;

	if ( $IS_SUCCESS(__cli$get_value(a_clictx, &s_quals[QUAL$K_TIMEOUT], &l_val)) )
		a_ctx->cc_timeout = atoi($ASCPTR(&l_val));

	if ( $IS_SUCCESS(__cli$get_value(a_clictx, &s_quals[QUAL$K_MANAGER], NULL)) )
		a_ctx->cc_manager = 1;

	if ( $IS_SUCCESS(__cli$get_value(a_clictx, &s_quals[QUAL$K_QUEUES], NULL)) )
		a_ctx->cc_queues = 1;

	if ( $IS_SUCCESS(__cli$get_value(a_clictx, &s_quals[QUAL$K_PAUSE], NULL)) )
		a_ctx->cc_pause = 1;

	if ( $IS_SUCCESS(__cli$get_value(a_clictx, &s_quals[QUAL$K_RESUME], NULL)) )
		a_ctx->cc_resume = 1;

	if ( $IS_SUCCESS(__cli$get_value(a_clictx, &s_quals[QUAL$K_CONFIRM], NULL)) )
		a_ctx->cc_confirm = 1;

	if ( $IS_SUCCESS(__cli$get_value(a_clictx, &s_quals[QUAL$K_NOCONFIRM], NULL)) )
		a_ctx->cc_noconfirm = 1;

	/*
	**  The processor list is verified here rather than left to the controller: a mistake in it is
	**  the submitter's, and the submitter is the one standing at the terminal.
	*/
	if ( $IS_SUCCESS(__cli$get_value(a_clictx, &s_quals[QUAL$K_CPU], &l_val)) )
		{
		if ( $IS_FAILURE(util$parse_cpulist($ASCPTR(&l_val), &l_cpus, &l_ncpu)) )
			return	$PUTMSG_FAO(BATCH$_IVCPU, $ASCPTR(&l_val));

		util$mkfspec(a_ctx->cc_cpus, sizeof(a_ctx->cc_cpus), "!AC",
				(const fao_prm_t []) { (fao_prm_t) &l_val }, 1);
		}

	if ( $IS_SUCCESS(__cli$get_value(a_clictx, &s_quals[QUAL$K_NAME], &l_val)) )
		util$mkfspec(a_ctx->cc_name, sizeof(a_ctx->cc_name), "!AC", (const fao_prm_t []) { (fao_prm_t) &l_val }, 1);

	if ( $IS_SUCCESS(__cli$get_value(a_clictx, &s_quals[QUAL$K_QUEUE], &l_val)) )
		util$mkfspec(a_ctx->cc_queue, sizeof(a_ctx->cc_queue), "!AC", (const fao_prm_t []) { (fao_prm_t) &l_val }, 1);

	if ( $IS_SUCCESS(__cli$get_value(a_clictx, &s_quals[QUAL$K_PRIORITY], &l_val)) )
		{
		a_ctx->cc_prio		= atoi($ASCPTR(&l_val));
		a_ctx->cc_prioset	= 1;
		}

	if ( $IS_SUCCESS(__cli$get_value(a_clictx, &s_quals[QUAL$K_ENTRY], &l_val)) )
		{
		__util$asc2str(&l_val, a_ctx->cc_entrystr);

		a_ctx->cc_entry = (unsigned int) strtoul(a_ctx->cc_entrystr, NULL, 10);
		}

	/* The keyword was validated against the list, so an unknown one cannot reach this point */
	if ( $IS_SUCCESS(__cli$get_value(a_clictx, &s_quals[QUAL$K_RETAIN], &l_val)) )
		{
		a_ctx->cc_retainset = 1;

		if ( !strncasecmp($ASCPTR(&l_val), "ALWAYS", 3) )
			a_ctx->cc_retain = RETAIN$K_ALWAYS;
		else if ( !strncasecmp($ASCPTR(&l_val), "NEVER", 3) )
			a_ctx->cc_retain = RETAIN$K_NEVER;
		else
			a_ctx->cc_retain = RETAIN$K_ERROR;
		}

	if ( $IS_SUCCESS(__cli$get_value(a_clictx, &s_quals[QUAL$K_PARAMETERS], &l_val)) )
		{
		if ( $IS_FAILURE(l_sts = s_batch$parse_parms($ASCPTR(&l_val), a_ctx)) )
			return	l_sts;
		}

	if ( $IS_SUCCESS(__cli$get_value(a_clictx, &s_quals[QUAL$K_AFTER], &l_val)) )
		{
		if ( $IS_FAILURE(util$parse_time($ASCPTR(&l_val), &l_tim)) )
			return	$PUTMSG_FAO(BATCH$_INVTIME, $ASCPTR(&l_val));

		a_ctx->cc_after = l_tim;
		}

	if ( $IS_FAILURE(__cli$get_value(a_clictx, &s_quals[QUAL$K_LOG_FILE], &l_val)) )
		return	BATCH$_NORMAL;

	/*
	**  The routine realpath(3) demands a receiving buffer of PATH_MAX bytes, so the path is
	**  resolved through a local buffer and transferred to the context with a truncation check.
	**  For a file that does not yet exist the directory is resolved and the name appended.
	*/
	if ( realpath($ASCPTR(&l_val), l_real) )
		return	util$mkfspec(a_ctx->cc_logfile, sizeof(a_ctx->cc_logfile), "!AZ",
					(const fao_prm_t []) { (fao_prm_t) l_real }, 1);

	util$mkfspec(l_dir, sizeof(l_dir), "!AC", (const fao_prm_t []) { (fao_prm_t) &l_val }, 1);
	l_p = strrchr(l_dir, '/');

	/* No separator means the name is relative to the current directory */
	if ( !l_p )
		{
		if ( !getcwd(l_real, sizeof(l_real)) )
			return	$PUTMSG_FAO(BATCH$_CWDERR, errno, strerror(errno));

		return	util$mkfspec(a_ctx->cc_logfile, sizeof(a_ctx->cc_logfile), "!AZ/!AZ",
				(const fao_prm_t []) { (fao_prm_t) l_real, (fao_prm_t) l_dir }, 2);
		}

	*l_p++ = '\0';

	if ( !realpath((l_dir[0]) ? l_dir : "/", l_real) )
		return	$PUTMSG_FAO(BATCH$_NODIR, l_dir, errno, strerror(errno));

	return	util$mkfspec(a_ctx->cc_logfile, sizeof(a_ctx->cc_logfile), "!AZ/!AZ",
				(const fao_prm_t []) { (fao_prm_t) l_real, (fao_prm_t) l_p }, 2);
}

/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Establishes a connection with the job controller over an AF_UNIX socket.
**
**  INPUT PARAMETERS:
**	None; the socket specification is built from the global root directory g_root.
**
**  OUTPUT PARAMETERS:
**	a_fd	- receives the descriptor of the connection, not NULL. On error the value -1.
**
**  RETURNS:
**	BATCH$_NORMAL, BATCH$_BUFOVF, BATCH$_SOCKPATH, BATCH$_SOCKERR or BATCH$_NOCONNECT.
**--
*/
static	int	s_batch$connect (
			int *	a_fd
			)
{
struct sockaddr_un	l_sa;
char			l_path [BATCH$K_PATHSZ];
int			l_fd, l_rc;

	*a_fd = -1;

	if ( $IS_FAILURE(util$mkfspec(l_path, sizeof(l_path), "!AZ/!AZ",
				(const fao_prm_t []) { (fao_prm_t) g_root, (fao_prm_t) IPC$K_SOCKNAME }, 2)) )
		return	BATCH$_BUFOVF;

	memset(&l_sa, 0, sizeof(l_sa));
	l_sa.sun_family = AF_UNIX;

	if ( $IS_FAILURE(util$mkfspec(l_sa.sun_path, sizeof(l_sa.sun_path), "!AZ",
				(const fao_prm_t []) { (fao_prm_t) l_path }, 1)) )
		return	BATCH$_SOCKPATH;

	if ( 0 > (l_fd = socket(AF_UNIX, SOCK_STREAM, 0)) )
		return	$PUTMSG_FAO(BATCH$_SOCKERR, l_fd, errno, strerror(errno));

	if ( 0 > (l_rc = connect(l_fd, (struct sockaddr *) &l_sa, sizeof(l_sa))) )
		{
		close(l_fd);

		return	$PUTMSG_FAO(BATCH$_NOCONNECT, l_path, l_rc, errno, strerror(errno));
		}

	*a_fd = l_fd;

	return	BATCH$_NORMAL;
}

/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Implements the SUBMIT command: assembles the job descriptor, transmits an IPC$K_SUBMIT request
**	and displays the queue entry number allocated by the job controller.
**
**	The script specification and the working directory are resolved here, on the client side: only
**	the client knows the current directory of the user, and the daemon receives absolute paths.
**
**  INPUT PARAMETERS:
**	a_script - specification of the shell script, not NULL.
**	a_ctx	 - the parsed qualifiers, not NULL.
**
**  OUTPUT PARAMETERS:
**	None; the entry number is written to the standard output.
**
**  RETURNS:
**	BATCH$_NORMAL. On rejection, either a condition of the utility itself or the condition produced
**	by the job controller and received in the mh_sts field of the reply.
**--
*/
static	int	s_batch$do_submit (
			const char *	a_script,
			BATCH$CLICTX *	a_ctx
			)
{
BATCH$JOB	l_job;
BATCH$IPCHDR	l_hdr, l_rsp;
char		l_path [PATH_MAX], l_tmp [BATCH$K_PATHSZ];
char		l_name [64], l_when [32];
const char *	l_queue;
int		l_fd, l_i, l_len, l_rc;

	memset(&l_job, 0, sizeof(l_job));

	if ( !realpath(a_script, l_path) )
		return	$PUTMSG_FAO(BATCH$_NOSCRIPT, a_script, "not found", "realpath", -1, errno,
					strerror(errno));

	if ( 0 > (l_rc = access(l_path, R_OK)) )
		return	$PUTMSG_FAO(BATCH$_NOSCRIPT, l_path, "not readable", "access", l_rc, errno,
					strerror(errno));

	if ( $IS_FAILURE(util$mkfspec(l_job.jb_script, sizeof(l_job.jb_script), "!AZ",
				(const fao_prm_t []) { (fao_prm_t) l_path }, 1)) )
		return	BATCH$_BUFOVF;

	/* The job starts in the directory that was current when it was submitted */
	if ( !getcwd(l_path, sizeof(l_path)) )
		util$mkfspec(l_path, sizeof(l_path), "!AZ", (const fao_prm_t []) { (fao_prm_t) "/" }, 1);

	if ( $IS_FAILURE(util$mkfspec(l_job.jb_cwd, sizeof(l_job.jb_cwd), "!AZ", (const fao_prm_t []) { (fao_prm_t) l_path }, 1)) )
		return	BATCH$_BUFOVF;

	/* By default the job name is the script file name with any .sh suffix removed */
	if ( a_ctx->cc_name[0] )
		util$mkfspec(l_job.jb_name, sizeof(l_job.jb_name), "!AZ", (const fao_prm_t []) { (fao_prm_t) a_ctx->cc_name }, 1);
	else
		{
		util$mkfspec(l_tmp, sizeof(l_tmp), "!AZ", (const fao_prm_t []) { (fao_prm_t) l_job.jb_script }, 1);
		util$mkfspec(l_job.jb_name, sizeof(l_job.jb_name), "!AZ", (const fao_prm_t []) { (fao_prm_t) basename(l_tmp) }, 1);
		l_len = (int) strlen(l_job.jb_name);

		if ( (l_len > 3) && (!strcasecmp(l_job.jb_name + l_len - 3, ".sh")) )
			l_job.jb_name[l_len - 3] = '\0';
		}

	util$mkfspec(l_job.jb_queue, sizeof(l_job.jb_queue), "!AZ", (const fao_prm_t []) { (fao_prm_t) a_ctx->cc_queue }, 1);
	util$mkfspec(l_job.jb_logfile, sizeof(l_job.jb_logfile), "!AZ", (const fao_prm_t []) { (fao_prm_t) a_ctx->cc_logfile }, 1);

	for ( l_i = 0; l_i < BATCH$K_NPARM; l_i++ )
		util$mkfspec(l_job.jb_parm[l_i], BATCH$K_PARMSZ, "!AZ",
					(const fao_prm_t []) { (fao_prm_t) a_ctx->cc_parm[l_i] }, 1);

	l_job.jb_prio		= (unsigned int) ((a_ctx->cc_prio) ? a_ctx->cc_prio : BATCH$K_DEFPRIO);
	l_job.jb_retain		= (unsigned int) a_ctx->cc_retain;
	l_job.jb_keeplog	= a_ctx->cc_keep;
	l_job.jb_restart	= a_ctx->cc_restart;

	util$mkfspec(l_job.jb_user, sizeof(l_job.jb_user), "!AZ",
			(const fao_prm_t []) { (fao_prm_t) a_ctx->cc_user }, 1);

	/*
	**  Where no log was named, it goes into the directory the job is submitted from, as it does in
	**  OpenVMS.  Whether that directory can be written in is asked here rather than by the
	**  controller: this image runs as the submitter, the controller ordinarily as the privileged
	**  user, and the question is about the submitter.  Where the answer is no the log falls back
	**  to the log directory of the subsystem, which is what it was before there was a choice.
	*/
	l_job.jb_logcwd		= (!a_ctx->cc_logfile[0]) && (!access(l_job.jb_cwd, W_OK | X_OK));

	util$mkfspec(l_job.jb_cpus, sizeof(l_job.jb_cpus), "!AZ",
			(const fao_prm_t []) { (fao_prm_t) a_ctx->cc_cpus }, 1);
	l_job.jb_after		= a_ctx->cc_after;
	l_job.jb_state		= (a_ctx->cc_hold) ? JOB$K_HOLDING : JOB$K_PENDING;

	if ( $IS_FAILURE(s_batch$connect(&l_fd)) )
		return	BATCH$_NOCONNECT;

	memset(&l_hdr, 0, sizeof(l_hdr));
	l_hdr.mh_magic	= IPC$K_MAGIC;
	l_hdr.mh_func	= IPC$K_SUBMIT;
	l_hdr.mh_len	= (unsigned int) sizeof(l_job);

	if ( $IS_FAILURE(util$write_exact(l_fd, &l_hdr, sizeof(l_hdr))) )
		{
		close(l_fd);

		return	$PUTMSG_FAO(BATCH$_SENDERR, "the SUBMIT request header");
		}

	if ( $IS_FAILURE(util$write_exact(l_fd, &l_job, sizeof(l_job))) )
		{
		close(l_fd);

		return	$PUTMSG_FAO(BATCH$_SENDERR, "the job descriptor");
		}

	if ( $IS_FAILURE(util$read_exact(l_fd, &l_rsp, sizeof(l_rsp))) )
		{
		close(l_fd);

		return	$PUTMSG_FAO(BATCH$_NOREPLY, "SUBMIT");
		}

	close(l_fd);

	/*
	**  The job controller returned a condition of its own. The arguments of its message do not
	**  cross the process boundary, so the condition is reported as a value and the readable text
	**  is left where it was issued in full - in the log of the job controller.
	*/
	if ( $IS_FAILURE((int) l_rsp.mh_sts) )
		{
		s_batch$condname(l_rsp.mh_sts, l_name, sizeof(l_name));
		$PUTMSG_FAO(BATCH$_SUBREJECT, l_name, l_rsp.mh_sts);

		return	(int) l_rsp.mh_sts;
		}

	/*
	**  The state the job was placed in is named, and where it is not to start before a time, that
	**  time is named too. A delta specification is easy to write and hard to read back - in
	**  "+[dddd-]hh[:mm[:ss]]" the day field is told from the hour field by a hyphen alone, so "+7"
	**  is seven hours and "+7-" is seven days - and a job held for the wrong span looks exactly
	**  like one held for the right span until that span has passed.
	*/
	l_queue = (a_ctx->cc_queue[0]) ? a_ctx->cc_queue : BATCH$K_DEFQUEUE;

	if ( l_job.jb_after )
		{
		util$fmt_time(l_job.jb_after, l_when, sizeof(l_when));

		$PUTMSG_FAO(BATCH$_QUEUEDUNTIL, l_job.jb_name, l_queue, l_rsp.mh_entry, l_when);
		}
	else if ( l_job.jb_state == JOB$K_HOLDING )
		$PUTMSG_FAO(BATCH$_QUEUEDHELD, l_job.jb_name, l_queue, l_rsp.mh_entry);
	else	$PUTMSG_FAO(BATCH$_QUEUED, l_job.jb_name, l_queue, l_rsp.mh_entry);

	return	BATCH$_NORMAL;
}

/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Writes one line of the brief tabular format of the SHOW command. For an executing job the
**	elapsed time is measured to the present moment, for a terminated one to the moment of
**	termination.
**
**  INPUT PARAMETERS:
**	a_job	- job descriptor, not NULL.
**
**  OUTPUT PARAMETERS:
**	None.
**
**  RETURNS:
**	BATCH$_NORMAL.
**--
*/
static	int	s_batch$show_brief (
			BATCH$JOB *	a_job
			)
{
const char *	l_state;
long long	l_elp, l_end;
char		l_tim [32], l_sts [32];

	util$fmt_time(a_job->jb_qtime, l_tim, sizeof(l_tim));
	util$state_name(a_job->jb_state, &l_state);

	l_elp = 0;

	if ( a_job->jb_stime )
		{
		l_end = (a_job->jb_etime) ? a_job->jb_etime : (long long) time(NULL);
		l_elp = l_end - a_job->jb_stime;
		}

	/*
	**  The last column says what became of the job, and for one that has not run yet it says what
	**  it is waiting for instead: a job held until a time carries no exit status, so the column
	**  would otherwise hold a dash where the one thing worth knowing about such a job is when it
	**  starts. That was worth a wider column - the alternative being /FULL for a fact that belongs
	**  in the summary.
	**
	**  A signal takes precedence over an exit status, both over the waiting time: a job that has
	**  run is described by how it ended.
	*/
	if ( a_job->jb_signal )
		util$mkfspec(l_sts, sizeof(l_sts), "signal=!SL", (const fao_prm_t []) { (fao_prm_t) a_job->jb_signal }, 1);
	else if ( a_job->jb_exit )
		util$mkfspec(l_sts, sizeof(l_sts), "exit=!SL", (const fao_prm_t []) { (fao_prm_t) a_job->jb_exit }, 1);
	else if ( (a_job->jb_state == JOB$K_HOLDING) && (a_job->jb_after) )
		util$fmt_time(a_job->jb_after, l_sts, sizeof(l_sts));
	else
		util$mkfspec(l_sts, sizeof(l_sts), "-", NULL, 0);

	util$put("!6UL  !20<!AZ!> !10<!AZ!> !20<!AZ!> !2ZL:!2ZL:!2ZL  !20<!AZ!>\n",
			(const fao_prm_t []) { (fao_prm_t) a_job->jb_entry, (fao_prm_t) a_job->jb_name,
						(fao_prm_t) l_state, (fao_prm_t) l_tim,
						(fao_prm_t) (l_elp / 3600), (fao_prm_t) ((l_elp / 60) % 60),
						(fao_prm_t) (l_elp % 60), (fao_prm_t) l_sts }, 8);

	return	BATCH$_NORMAL;
}

/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Writes the full description of a job for the /FULL format.
**
**  INPUT PARAMETERS:
**	a_job	- job descriptor, not NULL.
**
**  OUTPUT PARAMETERS:
**	None.
**
**  RETURNS:
**	BATCH$_NORMAL.
**--
*/
static	int	s_batch$show_full (
			BATCH$JOB *	a_job
			)
{
struct passwd *	l_pw;
const char *	l_state;
char		l_tim [32];
int		l_i, l_any;

	util$state_name(a_job->jb_state, &l_state);
	l_pw = getpwuid((uid_t) a_job->jb_uid);

	util$put("\nEntry !UL, job !AZ, queue !AZ\n",
			(const fao_prm_t []) { (fao_prm_t) a_job->jb_entry, (fao_prm_t) a_job->jb_name,
						(fao_prm_t) a_job->jb_queue }, 3);

	if ( a_job->jb_state == JOB$K_EXECUTING )
		util$put("    State ............ !AZ, pid !SL\n",
				(const fao_prm_t []) { (fao_prm_t) l_state, (fao_prm_t) a_job->jb_pid }, 2);
	else
		util$put("    State ............ !AZ\n", (const fao_prm_t []) { (fao_prm_t) l_state }, 1);

	util$put("    Owner ............ !AZ (uid !SL, gid !SL)\n",
			(const fao_prm_t []) { (fao_prm_t) ((l_pw) ? l_pw->pw_name : "?"),
						(fao_prm_t) a_job->jb_uid, (fao_prm_t) a_job->jb_gid }, 3);
	util$put("    Priority ......... !UL\n", (const fao_prm_t []) { (fao_prm_t) a_job->jb_prio }, 1);

	if ( a_job->jb_cpus[0] )
		util$put("    Processors ....... !AZ\n", (const fao_prm_t []) { (fao_prm_t) a_job->jb_cpus }, 1);

	if ( a_job->jb_restart )
		util$put("    Restart .......... yes, restarted !UL time!%S\n",
				(const fao_prm_t []) { (fao_prm_t) a_job->jb_restarts }, 1);
	util$put("    Script ........... !AZ\n", (const fao_prm_t []) { (fao_prm_t) a_job->jb_script }, 1);
	util$put("    Working directory  !AZ\n", (const fao_prm_t []) { (fao_prm_t) a_job->jb_cwd }, 1);
	util$put("    Log file ......... !AZ!AZ\n",
			(const fao_prm_t []) { (fao_prm_t) a_job->jb_logfile,
						(fao_prm_t) ((a_job->jb_keeplog) ? " (/KEEP)" : "") }, 2);

	util$fmt_time(a_job->jb_qtime, l_tim, sizeof(l_tim));
	util$put("    Submitted ........ !AZ\n", (const fao_prm_t []) { (fao_prm_t) l_tim }, 1);

	if ( a_job->jb_after )
		{
		util$fmt_time(a_job->jb_after, l_tim, sizeof(l_tim));
		util$put("    Not before ....... !AZ\n", (const fao_prm_t []) { (fao_prm_t) l_tim }, 1);
		}

	if ( a_job->jb_stime )
		{
		util$fmt_time(a_job->jb_stime, l_tim, sizeof(l_tim));
		util$put("    Started .......... !AZ\n", (const fao_prm_t []) { (fao_prm_t) l_tim }, 1);
		}

	if ( a_job->jb_etime )
		{
		util$fmt_time(a_job->jb_etime, l_tim, sizeof(l_tim));
		util$put("    Terminated ....... !AZ\n", (const fao_prm_t []) { (fao_prm_t) l_tim }, 1);

		if ( a_job->jb_signal )
			util$put("    Exit status ...... !SL, signal !SL\n",
					(const fao_prm_t []) { (fao_prm_t) a_job->jb_exit,
								(fao_prm_t) a_job->jb_signal }, 2);
		else
			util$put("    Exit status ...... !SL\n",
					(const fao_prm_t []) { (fao_prm_t) a_job->jb_exit }, 1);
		}

	/* The parameters are listed in a column, the heading printed before the first only */
	l_any = 0;

	for ( l_i = 0; l_i < BATCH$K_NPARM; l_i++ )
		{
		if ( !a_job->jb_parm[l_i][0] )
			continue;

		util$put("!AZP!SL = \"!AZ\"\n",
				(const fao_prm_t []) { (fao_prm_t) ((l_any++) ? "                       "
									: "    Parameters ....... "),
							(fao_prm_t) (l_i + 1),
							(fao_prm_t) a_job->jb_parm[l_i] }, 3);
		}

	return	BATCH$_NORMAL;
}

/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Implements the SHOW command: transmits an IPC$K_SHOW request, receives the selection of
**	entries and displays it in either the brief or the full format.
**
**  INPUT PARAMETERS:
**	a_ctx	- the parsed qualifiers, not NULL.
**
**  OUTPUT PARAMETERS:
**	None; the selection is written to the standard output.
**
**  RETURNS:
**	BATCH$_NORMAL, BATCH$_NOCONNECT, BATCH$_SENDERR, BATCH$_NOREPLY or BATCH$_RECVERR.
**--
*/
static	int	s_batch$do_show (
			BATCH$CLICTX *	a_ctx
			)
{
BATCH$JOB	l_job;
BATCH$IPCHDR	l_hdr, l_rsp;
BATCH$SHOWREQ	l_req;
char		l_what [96];
int		l_fd;
unsigned int	l_i;

	if ( $IS_FAILURE(s_batch$connect(&l_fd)) )
		return	BATCH$_NOCONNECT;

	memset(&l_req, 0, sizeof(l_req));
	l_req.sr_flags = a_ctx->cc_showflg;
	l_req.sr_entry = a_ctx->cc_entry;
	util$mkfspec(l_req.sr_queue, sizeof(l_req.sr_queue), "!AZ", (const fao_prm_t []) { (fao_prm_t) a_ctx->cc_queue }, 1);

	memset(&l_hdr, 0, sizeof(l_hdr));
	l_hdr.mh_magic	= IPC$K_MAGIC;
	l_hdr.mh_func	= IPC$K_SHOW;
	l_hdr.mh_len	= (unsigned int) sizeof(l_req);

	if ( $IS_FAILURE(util$write_exact(l_fd, &l_hdr, sizeof(l_hdr))) )
		{
		close(l_fd);

		return	$PUTMSG_FAO(BATCH$_SENDERR, "the SHOW request header");
		}

	if ( $IS_FAILURE(util$write_exact(l_fd, &l_req, sizeof(l_req))) )
		{
		close(l_fd);

		return	$PUTMSG_FAO(BATCH$_SENDERR, "the selection request");
		}

	if ( $IS_FAILURE(util$read_exact(l_fd, &l_rsp, sizeof(l_rsp))) )
		{
		close(l_fd);

		return	$PUTMSG_FAO(BATCH$_NOREPLY, "SHOW");
		}

	/*
	**  Paging is turned on before the first line and off after the last, so that what is held at
	**  the foot of a screen is the display of the queue and nothing else - a diagnostic issued
	**  afterwards is not a thing to make the reader press a key for.
	*/
	if ( a_ctx->cc_page )
		util$page(1);

	if ( !l_rsp.mh_count )
		{
		close(l_fd);

		/*
		**  Where an entry was named, its absence is a condition rather than an empty display: the
		**  enquirer asked about one entry and is owed an answer about that entry, not a remark
		**  about the state of the queue at large.
		*/
		util$page(0);

		if ( a_ctx->cc_entry )
			return	$PUTMSG_FAO(BATCH$_NOSUCHENT, a_ctx->cc_entry);

		/*
		**  The absence of jobs is a remark about the enquiry rather than a part of its answer, so
		**  it is issued as a condition like everything else this subsystem says of itself. The
		**  display proper - the table of entries - stays plain text: that is data, read and taken
		**  apart by whoever asked for it.
		*/
		return	$PUTMSG_FAO((a_ctx->cc_showflg & SHOW$M_ALL) ? BATCH$_EMPTYQUE : BATCH$_NOJOBS);
		}

	if ( !(a_ctx->cc_showflg & SHOW$M_FULL) )
		{
		util$put("\n Entry  Job name             State      Submitted            Elapsed   Status / not before\n",
				NULL, 0);
		util$put(" !5*-  !20*- !10*- !20*- !9*- !20*-\n", NULL, 0);
		}

	for ( l_i = 0; l_i < l_rsp.mh_count; l_i++ )
		{
		if ( $IS_FAILURE(util$read_exact(l_fd, &l_job, sizeof(l_job))) )
			{
			close(l_fd);

			util$mkfspec(l_what, sizeof(l_what), "the reply, which ended at record !UL of !UL",
					(const fao_prm_t []) { (fao_prm_t) l_i,
								(fao_prm_t) l_rsp.mh_count }, 2);

			util$page(0);

			return	$PUTMSG_FAO(BATCH$_RECVERR, l_what);
			}

		if ( a_ctx->cc_showflg & SHOW$M_FULL )
			s_batch$show_full(&l_job);
		else
			s_batch$show_brief(&l_job);
		}

	close(l_fd);
	util$put("\nTotal entries: !UL\n", (const fao_prm_t []) { (fao_prm_t) l_rsp.mh_count }, 1);

	util$page(0);

	return	BATCH$_NORMAL;
}

/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Implements the DELETE, RELEASE and SET ENTRY commands for one entry: transmits a short request,
**	the entry number being carried in a field of the header and the payload, where the function
**	has one, following it.
**
**  INPUT PARAMETERS:
**	a_func	- function code, IPC$K_DELETE, IPC$K_RELEASE or IPC$K_SETENTRY.
**	a_entry	- queue entry number; zero is not admissible.
**	a_pld	- the payload, or NULL where the function carries none.
**	a_pldsz	- its size in bytes, zero for none.
**
**  OUTPUT PARAMETERS:
**	None.
**
**  RETURNS:
**	BATCH$_NORMAL. On rejection, either a condition of the utility itself or one produced by the
**	job controller.
**--
*/
static	int	s_batch$do_entryop (
			unsigned int	a_func,
			unsigned int	a_entry,
			const void *	a_pld,
			unsigned int	a_pldsz
			)
{
BATCH$IPCHDR	l_hdr, l_rsp;
const char *	l_verb;
char		l_name [64], l_what [BATCH$K_NAMESZ + BATCH$K_CPUSZ + 160];
int		l_fd;

	if ( !a_entry )
		return	$PUTMSG_FAO(BATCH$_NOENTRY);

	l_verb = (a_func == IPC$K_DELETE) ? "DELETE" : (a_func == IPC$K_RELEASE) ? "RELEASE" : "SET ENTRY";

	if ( $IS_FAILURE(s_batch$connect(&l_fd)) )
		return	BATCH$_NOCONNECT;

	memset(&l_hdr, 0, sizeof(l_hdr));
	l_hdr.mh_magic	= IPC$K_MAGIC;
	l_hdr.mh_func	= a_func;
	l_hdr.mh_entry	= a_entry;
	l_hdr.mh_len	= (a_pld) ? a_pldsz : 0;

	if ( $IS_FAILURE(util$write_exact(l_fd, &l_hdr, sizeof(l_hdr)))
		|| ((a_pld) && ($IS_FAILURE(util$write_exact(l_fd, a_pld, a_pldsz)))) )
		{
		close(l_fd);

		return	$PUTMSG_FAO(BATCH$_SENDERR, "the request");
		}

	if ( $IS_FAILURE(util$read_exact(l_fd, &l_rsp, sizeof(l_rsp))) )
		{
		close(l_fd);

		return	$PUTMSG_FAO(BATCH$_NOREPLY, l_verb);
		}

	close(l_fd);

	/*
	**  The job controller returned a condition of its own. The arguments of its message do not
	**  cross the process boundary, so the condition is reported as a value and the readable text
	**  is left where it was issued in full - in the log of the job controller.
	*/
	if ( $IS_FAILURE((int) l_rsp.mh_sts) )
		{
		s_batch$condname(l_rsp.mh_sts, l_name, sizeof(l_name));
		$PUTMSG_FAO(BATCH$_SUBREJECT, l_name, l_rsp.mh_sts);

		return	(int) l_rsp.mh_sts;
		}

	/*
	**  The outcome is issued as a condition rather than written as a line of its own: everything
	**  this subsystem says of itself carries the facility, the severity and a mnemonic, and an
	**  operation that reported its success in plain words was the one place it did not.
	*/
	switch ( a_func )
		{
		case	IPC$K_DELETE:
			$PUTMSG_FAO(BATCH$_ENTDELETED, a_entry);
			break;

		case	IPC$K_RELEASE:
			$PUTMSG_FAO(BATCH$_ENTRELEASED, a_entry);
			break;

		case	IPC$K_SETENTRY:
			util$fmt_setent((const BATCH$SETENT *) a_pld, l_what, sizeof(l_what));
			$PUTMSG_FAO(BATCH$_ENTMODIFIED, a_entry, l_what);
			break;

		default:
			break;
		}

	return	BATCH$_NORMAL;
}

/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Renders a condition value the job controller sent back as the mnemonic of its message.
**
**	The arguments of a message do not cross the connection - only the condition value does - so
**	the text cannot be formatted here: its conversions would consume arguments that were never
**	sent. What can be shown is the part before the first comma, which is the standard prefix
**	%FACILITY-S-IDENT and carries no conversion of its own.
**
**	That is worth doing because the condition alone is opaque at the terminal, while the log of
**	the controller, where the whole message was issued, is often not to hand and may not be
**	readable by whoever entered the command.
**
**  INPUT PARAMETERS:
**	a_sts	- the condition value.
**
**  OUTPUT PARAMETERS:
**	a_buf	- buffer receiving the mnemonic, not NULL. On failure the condition is rendered in
**		  hexadecimal instead, so the caller always has something to show.
**	a_bufsz	- size of that buffer.
**
**  RETURNS:
**	BATCH$_NORMAL.
**--
*/
static	int	s_batch$condname (
			unsigned int	a_sts,
			char *		a_buf,
			size_t		a_bufsz
			)
{
EMSG_RECORD *	l_rec;
const char *	l_end;
size_t		l_len;

	if ( $IS_SUCCESS(__util$getmsg(a_sts, &l_rec)) )
		{
		l_end	= strchr(l_rec->text, ',');
		l_len	= (l_end) ? (size_t) (l_end - l_rec->text) : strlen(l_rec->text);

		/* The text of a record carries the per cent sign doubled, as printf wants it */
		if ( (l_len > 1) && (l_rec->text[0] == '%') && (l_rec->text[1] == '%') )
			{
			l_len--;

			if ( l_len < a_bufsz )
				{
				memcpy(a_buf, l_rec->text + 1, l_len);
				a_buf[l_len] = '\0';

				return	BATCH$_NORMAL;
				}
			}
		else if ( l_len < a_bufsz )
			{
			memcpy(a_buf, l_rec->text, l_len);
			a_buf[l_len] = '\0';

			return	BATCH$_NORMAL;
			}
		}

	util$mkfspec(a_buf, a_bufsz, "condition !XL", (const fao_prm_t []) { (fao_prm_t) a_sts }, 1);

	return	BATCH$_NORMAL;
}

/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Reports the tags of a journal record that this revision does not assign, with their values in
**	hexadecimal.
**
**	The record is walked rather than asked by tag: what is wanted is precisely what was not asked
**	for elsewhere. The layout of a tagged value is that of AVPROTO_TLV - a tag, a length, and the
**	octets - and the payload length is in the header of the record.
**
**  INPUT PARAMETERS:
**	a_rec	- the record, not NULL.
**	a_len	- its length, header included.
**	a_entry	- entry number the record describes, for the report.
**
**  OUTPUT PARAMETERS:
**	None; the report is written to the standard output.
**
**  RETURNS:
**	BATCH$_NORMAL.
**--
*/
static	const char	s_digits [] = "0123456789ABCDEF";

static	int	s_batch$unknown_tags (
			const void *	a_rec,
			unsigned	a_len,
			unsigned int	a_entry
			)
{
const AVPROTO_PDU *	l_pdu = (const AVPROTO_PDU *) a_rec;
const AVPROTO_TLV *	l_tlv;
const unsigned char *	l_p, *l_end;
char			l_hex [3 * 16 + 1];
unsigned short		l_type, l_tag;
unsigned		l_vlen, l_i, l_n;

	if ( a_len < sizeof(AVPROTO_HDR) )
		return	BATCH$_NORMAL;

	l_p	= (const unsigned char *) a_rec + sizeof(AVPROTO_HDR);
	l_end	= (const unsigned char *) a_rec + a_len;

	while ( (size_t) (l_end - l_p) >= sizeof(AVPROTO_TLV) )
		{
		l_tlv	= (const AVPROTO_TLV *) l_p;
		l_vlen	= be16toh(l_tlv->w_len);

		__tlv$decode_tag(l_tlv->w_tag, &l_type, &l_tag);

		if ( (size_t) (l_end - l_p) < sizeof(AVPROTO_TLV) + l_vlen )
			break;

		if ( !batchjrn$known(l_tag) )
			{
			/* At most sixteen octets: enough to recognize a value, short of a wall of digits */
			l_n = (l_vlen > 16) ? 16 : l_vlen;
			l_hex[0] = '\0';

			/*
			**  The octets are rendered here rather than by the FAO engine: that engine has no
			**  directive for a zero-filled hexadecimal byte, and the alternative - a directive
			**  per octet - would cost more than the four lines it saves.
			*/
			for ( l_i = 0; l_i < l_n; l_i++ )
				{
				l_hex[3 * l_i]		= s_digits[(l_tlv->b_val[l_i] >> 4) & 0x0F];
				l_hex[3 * l_i + 1]	= s_digits[l_tlv->b_val[l_i] & 0x0F];
				l_hex[3 * l_i + 2]	= ' ';
				}

			l_hex[(l_n) ? (3 * l_n - 1) : 0] = '\0';

			util$put("  entry !UL: tag !UL, type !UL, !UL octet!%S not known to this revision: !AZ!AZ\n",
					(const fao_prm_t []) { (fao_prm_t) a_entry, (fao_prm_t) l_tag,
								(fao_prm_t) l_type, (fao_prm_t) l_vlen,
								(fao_prm_t) l_hex,
								(fao_prm_t) ((l_vlen > 16) ? " ..." : "") }, 6);
			}

		l_p += sizeof(AVPROTO_TLV) + l_vlen;
		}

	return	BATCH$_NORMAL;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Waits for a job to end and reports how it ended.
**
**	It is meant to be called from a script that has work to do after a job and no other way of
**	knowing when the job is done. The exit status carries the answer, so the caller need not read
**	the display at all: zero where the job succeeded, non-zero where it failed or where the wait
**	ran out.
**
**	The waiting is done by asking after the entry rather than by holding a connection open until
**	the job ends. A connection held would have to be excused the timeout that guards against a
**	client which has stopped talking, and the guard is worth more than the second of delay that
**	polling costs: a batch job is not a thing whose end must be learned within the second.
**
**	An entry that has left the queue is taken to have succeeded, because that is what makes an
**	entry leave: a job that fails is retained, and is found and reported.
**
**  INPUT PARAMETERS:
**	a_ctx	- the parsed command line, not NULL. The entry is cc_entry and the limit cc_timeout,
**		  in seconds, zero meaning no limit.
**
**  OUTPUT PARAMETERS:
**	None.
**
**  RETURNS:
**	BATCH$_SYNCDONE, BATCH$_SYNCFAIL or BATCH$_SYNCTMO.
**--
*/
static	int	s_batch$do_sync (
			const BATCH$CLICTX *	a_ctx
			)
{
BATCH$IPCHDR	l_hdr, l_rsp;
BATCH$SHOWREQ	l_req;
BATCH$JOB	l_job;
char		l_how [64];
long long	l_began = (long long) time(NULL);
int		l_fd, l_seen = 0;

	memset(&l_job, 0, sizeof(l_job));

	for ( ;; )
		{
		memset(&l_req, 0, sizeof(l_req));

		l_req.sr_flags	= SHOW$M_ALL | SHOW$M_EVERYONE;
		l_req.sr_entry	= a_ctx->cc_entry;

		if ( $IS_FAILURE(s_batch$connect(&l_fd)) )
			return	BATCH$_SYNCFAIL;

		memset(&l_hdr, 0, sizeof(l_hdr));

		l_hdr.mh_magic	= IPC$K_MAGIC;
		l_hdr.mh_func	= IPC$K_SHOW;
		l_hdr.mh_len	= (unsigned int) sizeof(l_req);
		l_hdr.mh_entry	= a_ctx->cc_entry;

		if ( $IS_FAILURE(util$write_exact(l_fd, &l_hdr, sizeof(l_hdr)))
			|| ($IS_FAILURE(util$write_exact(l_fd, &l_req, sizeof(l_req))))
			|| ($IS_FAILURE(util$read_exact(l_fd, &l_rsp, sizeof(l_rsp)))) )
			{
			close(l_fd);

			return	$PUTMSG_FAO(BATCH$_NOREPLY, "SYNC");
			}

		if ( $IS_FAILURE(l_rsp.mh_sts) )
			{
			char	l_name [64];

			close(l_fd);

			s_batch$condname(l_rsp.mh_sts, l_name, sizeof(l_name));

			return	$PUTMSG_FAO(BATCH$_SUBREJECT, l_name, l_rsp.mh_sts);
			}

		if ( l_rsp.mh_count )
			{
			l_seen = 1;

			if ( $IS_FAILURE(util$read_exact(l_fd, &l_job, sizeof(l_job))) )
				{
				close(l_fd);

				return	$PUTMSG_FAO(BATCH$_RECVERR, "the reply to SYNC");
				}
			}

		close(l_fd);

		/*
		**  Gone from the queue: it succeeded, that being what makes an entry leave. Retained: it
		**  ended, and the record says how. Anything else: it is still on its way.
		*/
		if ( !l_rsp.mh_count )
			{
			/*
			**  Gone, or never there. The counter of issued numbers tells the two apart: a number
			**  it never reached was never given out, whereas one it has passed belonged to a job
			**  which has since left the queue - and a job leaves the queue by succeeding.
			*/
			if ( (!l_seen) && (a_ctx->cc_entry > l_rsp.mh_entry) )
				return	$PUTMSG_FAO(BATCH$_NOSUCHENT, a_ctx->cc_entry);

			return	$PUTMSG_FAO(BATCH$_SYNCDONE, a_ctx->cc_entry);
			}

		if ( l_job.jb_state == JOB$K_RETAINED )
			{
			if ( l_job.jb_signal )
				util$mkfspec(l_how, sizeof(l_how), "signal=!SL",
						(const fao_prm_t []) { (fao_prm_t) l_job.jb_signal }, 1);
			else	util$mkfspec(l_how, sizeof(l_how), "exit=!SL",
						(const fao_prm_t []) { (fao_prm_t) l_job.jb_exit }, 1);

			if ( !l_job.jb_exit && !l_job.jb_signal )
				return	$PUTMSG_FAO(BATCH$_SYNCDONE, a_ctx->cc_entry);

			return	$PUTMSG_FAO(BATCH$_SYNCFAIL, a_ctx->cc_entry, l_how);
			}

		if ( (a_ctx->cc_timeout > 0)
			&& (((long long) time(NULL) - l_began) >= (long long) a_ctx->cc_timeout) )
			return	$PUTMSG_FAO(BATCH$_SYNCTMO, a_ctx->cc_entry, a_ctx->cc_timeout,
						(a_ctx->cc_timeout == 1) ? "" : "s");

		sleep(1);
		}
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Applies an operation to every entry a list names.
**
**	A list is written as DCL writes one: numbers and ranges separated by commas, the parentheses
**	optional - 1,3,4 and (1,3-5) alike. It was tempting to take the first number and be done, and
**	that is what was done before; the rest went by without a word, which is the worst of the
**	possible answers - the command reported success for the one entry it did dispose of.
**
**	Every element is applied, whatever became of those before it: a list is a list of requests,
**	not a sequence that stops at the first refusal. The status returned is the failure of the
**	last element that failed, so a script sees that something went wrong.
**
**  INPUT PARAMETERS:
**	a_func	- IPC$K_DELETE, IPC$K_RELEASE or IPC$K_SETENTRY.
**	a_list	- the list, not NULL.
**	a_pld	- the payload every element is sent with, or NULL where the function carries none.
**	a_pldsz	- its size in bytes, zero for none.
**
**  OUTPUT PARAMETERS:
**	None.
**
**  RETURNS:
**	BATCH$_NORMAL where every element succeeded, otherwise the condition of the last failure.
**--
*/
static	int	s_batch$do_entrylist (
			unsigned int	a_func,
			const char *	a_list,
			const void *	a_pld,
			unsigned int	a_pldsz
			)
{
char		l_work [BATCH$K_PATHSZ], *l_p, *l_next, *l_dash;
unsigned int	l_from, l_to, l_i;
int		l_sts = BATCH$_NORMAL, l_one;

	util$mkfspec(l_work, sizeof(l_work), "!AZ", (const fao_prm_t []) { (fao_prm_t) a_list }, 1);

	/* The parentheses DCL puts round a list, and the blanks within them, carry no meaning here */
	for ( char *l_r = l_work, *l_w = l_work; ; l_r++ )
		{
		if ( (*l_r != '(') && (*l_r != ')') && (*l_r != ' ') && (*l_r != '\t') )
			*l_w++ = *l_r;

		if ( !*l_r )
			break;
		}

	for ( l_p = l_work; (l_p) && (*l_p); l_p = l_next )
		{
		if ( (l_next = strchr(l_p, ',')) )
			*l_next++ = '\0';

		if ( !*l_p )
			continue;

		/* A range, or a single number - a hyphen after the first digit tells them apart */
		l_dash	= strchr(l_p + 1, '-');
		l_from	= (unsigned int) strtoul(l_p, NULL, 10);
		l_to	= (l_dash) ? (unsigned int) strtoul(l_dash + 1, NULL, 10) : l_from;

		if ( (!l_from) || (l_to < l_from) )
			{
			l_sts = $PUTMSG_FAO(BATCH$_IVENTRY, l_p);

			continue;
			}

		for ( l_i = l_from; l_i <= l_to; l_i++ )
			{
			if ( $IS_FAILURE(l_one = s_batch$do_entryop(a_func, l_i, a_pld, a_pldsz)) )
				l_sts = l_one;
			}
		}

	return	l_sts;
}

/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Implements the SET ENTRY command: gathers the attributes the qualifiers name into one request
**	and applies it to every entry of the list.
**
**	The request carries a mask of what it sets, so that an attribute not mentioned on the command
**	line is not touched: SET ENTRY 17 /PRIORITY=200 leaves the start time, the name and the rest
**	as they were, which is what a command that alters one thing is expected to do. A command that
**	names nothing to set is refused here rather than sent, there being nothing to send.
**
**	The priority and the processor list are verified here, where the submitter can be told at
**	once, as they are for SUBMIT; what depends on the state of the job - whether it may be altered
**	at all - is decided by the controller, which alone knows that state.
**
**  INPUT PARAMETERS:
**	a_list	- the list of entries, not NULL; an empty string is refused.
**	a_ctx	- the parsed command line, not NULL.
**
**  OUTPUT PARAMETERS:
**	None.
**
**  RETURNS:
**	BATCH$_NORMAL where every entry was altered, otherwise BATCH$_NOTHINGSET, BATCH$_IVPRIO,
**	BATCH$_NOENTRY or the condition of the last entry the controller refused.
**--
*/
static	int	s_batch$do_setentry (
			const char *		a_list,
			const BATCH$CLICTX *	a_ctx
			)
{
BATCH$SETENT	l_req;

	if ( !a_list[0] )
		return	$PUTMSG_FAO(BATCH$_NOENTRY);

	memset(&l_req, 0, sizeof(l_req));

	if ( a_ctx->cc_prioset )
		{
		if ( (a_ctx->cc_prio < 1) || (a_ctx->cc_prio > BATCH$K_MAXPRIO) )
			return	$PUTMSG_FAO(BATCH$_IVPRIO, BATCH$K_MAXPRIO);

		l_req.se_mask	|= SETENT$M_PRIORITY;
		l_req.se_prio	= (unsigned int) a_ctx->cc_prio;
		}

	/* /NOAFTER clears the time; /AFTER sets it, and the last of the two written wins */
	if ( a_ctx->cc_noafter )
		l_req.se_mask	|= SETENT$M_AFTER;
	else if ( a_ctx->cc_after )
		{
		l_req.se_mask	|= SETENT$M_AFTER;
		l_req.se_after	= a_ctx->cc_after;
		}

	if ( (a_ctx->cc_hold) || (a_ctx->cc_nohold) )
		{
		l_req.se_mask	|= SETENT$M_HOLD;
		l_req.se_hold	= a_ctx->cc_hold;
		}

	if ( a_ctx->cc_name[0] )
		{
		l_req.se_mask	|= SETENT$M_NAME;
		util$mkfspec(l_req.se_name, sizeof(l_req.se_name), "!AZ", (const fao_prm_t []) { (fao_prm_t) a_ctx->cc_name }, 1);
		}

	if ( a_ctx->cc_retainset )
		{
		l_req.se_mask	|= SETENT$M_RETAIN;
		l_req.se_retain	= (unsigned int) a_ctx->cc_retain;
		}

	if ( (a_ctx->cc_keep) || (a_ctx->cc_nokeep) )
		{
		l_req.se_mask	|= SETENT$M_KEEP;
		l_req.se_keep	= a_ctx->cc_keep;
		}

	if ( (a_ctx->cc_restart) || (a_ctx->cc_norestart) )
		{
		l_req.se_mask	|= SETENT$M_RESTART;
		l_req.se_restart = a_ctx->cc_restart;
		}

	if ( a_ctx->cc_cpus[0] )
		{
		l_req.se_mask	|= SETENT$M_CPU;
		util$mkfspec(l_req.se_cpus, sizeof(l_req.se_cpus), "!AZ", (const fao_prm_t []) { (fao_prm_t) a_ctx->cc_cpus }, 1);
		}

	if ( !l_req.se_mask )
		return	$PUTMSG_FAO(BATCH$_NOTHINGSET);

	return	s_batch$do_entrylist(IPC$K_SETENTRY, a_list, &l_req, (unsigned int) sizeof(l_req));
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Asks whether an irrevocable operation is to go ahead.
**
**	The answer is sought only where there is somebody to answer: redirected, the question would go
**	into a file and the reader would wait for a key nobody is at hand to press. A command in a
**	script is therefore taken as confirmed, which is what /NOCONFIRM says explicitly and what the
**	absence of a terminal says by itself.
**
**	Anything but a word beginning with Y is a refusal, the end of input included: the operation
**	cannot be undone, and silence is not consent.
**
**  INPUT PARAMETERS:
**	a_what	- what is about to happen, for the question.
**	a_ctx	- the parsed command line, not NULL.
**
**  OUTPUT PARAMETERS:
**	None.
**
**  RETURNS:
**	BATCH$_NORMAL to go ahead, BATCH$_NOTCONF otherwise.
**--
*/
static	int	s_batch$confirm (
			const char *		a_what,
			const BATCH$CLICTX *	a_ctx
			)
{
char	l_ans [16];
int	l_n = 0;
char	l_ch;

	if ( a_ctx->cc_noconfirm )
		return	BATCH$_NORMAL;

	if ( (!a_ctx->cc_confirm) && (!isatty(STDIN_FILENO)) )
		return	BATCH$_NORMAL;

	util$put("!AZ [y/N]: ", (const fao_prm_t []) { (fao_prm_t) a_what }, 1);

	while ( 1 == read(STDIN_FILENO, &l_ch, 1) )
		{
		if ( l_ch == '\n' )
			break;

		if ( l_n < (int) sizeof(l_ans) - 1 )
			l_ans[l_n++] = l_ch;
		}

	l_ans[l_n] = '\0';

	if ( (l_ans[0] == 'y') || (l_ans[0] == 'Y') )
		return	BATCH$_NORMAL;

	return	$PUTMSG_FAO(BATCH$_NOTCONF);
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Removes the entries of jobs that have finished.
**
**	It is the tidying that a queue which keeps its failures needs: an entry retained after a job
**	failed stays until somebody has looked at it, and looking at it is the whole point - but the
**	looking is done, and the entries remain.
**
**	Only a job that has finished is taken. A job still executing is not touched: PURGE tidies what
**	is over, it does not stop what is not.
**
**  INPUT PARAMETERS:
**	a_ctx	- the parsed command line, not NULL. /QUEUE restricts it to one queue.
**
**  OUTPUT PARAMETERS:
**	None.
**
**  RETURNS:
**	BATCH$_NORMAL, BATCH$_NOTCONF where it was not confirmed, or a condition of the connection.
**--
*/
static	int	s_batch$do_purge (
			const BATCH$CLICTX *	a_ctx
			)
{
BATCH$IPCHDR	l_hdr, l_rsp;
BATCH$SHOWREQ	l_req;
char		l_what [128];
int		l_fd;

	util$mkfspec(l_what, sizeof(l_what), "Remove the entries of the jobs that have finished!AZ!AZ",
			(const fao_prm_t []) { (fao_prm_t) ((a_ctx->cc_queue[0]) ? " in queue " : ""),
						(fao_prm_t) a_ctx->cc_queue }, 2);

	/*
	**  The refusal is told apart by its identification, not by its severity: BATCH$_NOTCONF is of
	**  informational severity - nothing went wrong, the operation simply did not happen - and an
	**  informational status carries the low bit set, which $IS_FAILURE reads as success.
	*/
	if ( $MATCH_COND(s_batch$confirm(l_what, a_ctx), BATCH$_NOTCONF) )
		return	BATCH$_NOTCONF;

	memset(&l_req, 0, sizeof(l_req));

	util$mkfspec(l_req.sr_queue, sizeof(l_req.sr_queue), "!AZ",
			(const fao_prm_t []) { (fao_prm_t) a_ctx->cc_queue }, 1);

	if ( $IS_FAILURE(s_batch$connect(&l_fd)) )
		return	BATCH$_NOCONNECT;

	memset(&l_hdr, 0, sizeof(l_hdr));

	l_hdr.mh_magic	= IPC$K_MAGIC;
	l_hdr.mh_func	= IPC$K_PURGE;
	l_hdr.mh_len	= (unsigned int) sizeof(l_req);

	if ( $IS_FAILURE(util$write_exact(l_fd, &l_hdr, sizeof(l_hdr)))
		|| ($IS_FAILURE(util$write_exact(l_fd, &l_req, sizeof(l_req))))
		|| ($IS_FAILURE(util$read_exact(l_fd, &l_rsp, sizeof(l_rsp)))) )
		{
		close(l_fd);

		return	$PUTMSG_FAO(BATCH$_NOREPLY, "PURGE");
		}

	close(l_fd);

	/*
	**  The condition the controller returned is looked at before the count. Without this the count
	**  of a refused request - zero, because nothing was counted - was reported as a purge that
	**  removed nothing, and the refusal itself was never seen. It shows itself where an old
	**  controller is still running under a new utility: the function code is unknown to it, it
	**  answers BATCH$_BADFUNC, and the queue stays as it was while the command reports success.
	*/
	if ( $IS_FAILURE(l_rsp.mh_sts) )
		{
		s_batch$condname(l_rsp.mh_sts, l_what, sizeof(l_what));

		return	$PUTMSG_FAO(BATCH$_SUBREJECT, l_what, l_rsp.mh_sts);
		}

	return	$PUTMSG_FAO(BATCH$_PURGED, l_rsp.mh_count, (l_rsp.mh_count == 1) ? "y" : "ies");
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Pauses or resumes a queue.
**
**	Pausing withholds the work of everybody who uses the queue, so the controller grants it to the
**	privileged user alone; this only carries the request.
**
**  INPUT PARAMETERS:
**	a_name	- the queue, not NULL; an empty string is refused.
**	a_ctx	- the parsed command line, not NULL.
**
**  OUTPUT PARAMETERS:
**	None.
**
**  RETURNS:
**	BATCH$_NORMAL, or the condition the controller returned.
**--
*/
static	int	s_batch$do_queueop (
			const char *		a_name,
			const BATCH$CLICTX *	a_ctx
			)
{
BATCH$IPCHDR	l_hdr, l_rsp;
BATCH$QUEOP	l_op;
char		l_name [64];
int		l_fd;

	if ( !a_name[0] )
		return	$PUTMSG_FAO(BATCH$_NOPARAM);

	if ( (!a_ctx->cc_pause) && (!a_ctx->cc_resume) )
		return	$PUTMSG_FAO(BATCH$_NOPARAM);

	memset(&l_op, 0, sizeof(l_op));

	l_op.qo_action = (a_ctx->cc_pause) ? QUEOP$K_PAUSE : QUEOP$K_RESUME;

	util$mkfspec(l_op.qo_name, sizeof(l_op.qo_name), "!AZ", (const fao_prm_t []) { (fao_prm_t) a_name }, 1);

	if ( $IS_FAILURE(s_batch$connect(&l_fd)) )
		return	BATCH$_NOCONNECT;

	memset(&l_hdr, 0, sizeof(l_hdr));

	l_hdr.mh_magic	= IPC$K_MAGIC;
	l_hdr.mh_func	= IPC$K_QUEUEOP;
	l_hdr.mh_len	= (unsigned int) sizeof(l_op);

	if ( $IS_FAILURE(util$write_exact(l_fd, &l_hdr, sizeof(l_hdr)))
		|| ($IS_FAILURE(util$write_exact(l_fd, &l_op, sizeof(l_op))))
		|| ($IS_FAILURE(util$read_exact(l_fd, &l_rsp, sizeof(l_rsp)))) )
		{
		close(l_fd);

		return	$PUTMSG_FAO(BATCH$_NOREPLY, "QUEUE");
		}

	close(l_fd);

	if ( $IS_FAILURE(l_rsp.mh_sts) )
		{
		s_batch$condname(l_rsp.mh_sts, l_name, sizeof(l_name));

		return	$PUTMSG_FAO(BATCH$_SUBREJECT, l_name, l_rsp.mh_sts);
		}

	$PUTMSG_FAO((a_ctx->cc_pause) ? BATCH$_QUEPAUSED : BATCH$_QUERESUMED, l_op.qo_name);

	return	BATCH$_NORMAL;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Displays the state of the job controller.
**
**	A display of the queue says what is in it; this says whether anybody is minding it. A queue
**	that stands still looks the same whether the controller is busy, idle, or gone - and the
**	first thing anyone wants to know when nothing is happening is which of the three it is.
**
**  INPUT PARAMETERS:
**	None.
**
**  OUTPUT PARAMETERS:
**	None; the display goes to the standard output.
**
**  RETURNS:
**	BATCH$_NORMAL, or a condition of the connection.
**--
*/
static	int	s_batch$do_manager (
			const BATCH$CLICTX *	a_ctx
			)
{
BATCH$IPCHDR	l_hdr, l_rsp;
BATCH$MGRSTAT	l_st;
char		l_when [32], l_up [32];
long long	l_elp;
int		l_fd;

	if ( $IS_FAILURE(s_batch$connect(&l_fd)) )
		return	BATCH$_NOCONNECT;

	memset(&l_hdr, 0, sizeof(l_hdr));

	l_hdr.mh_magic	= IPC$K_MAGIC;
	l_hdr.mh_func	= IPC$K_MGRSTAT;

	if ( $IS_FAILURE(util$write_exact(l_fd, &l_hdr, sizeof(l_hdr)))
		|| ($IS_FAILURE(util$read_exact(l_fd, &l_rsp, sizeof(l_rsp)))) )
		{
		close(l_fd);

		return	$PUTMSG_FAO(BATCH$_NOREPLY, "SHOW/MANAGER");
		}

	/* The condition first: a refusal carries no record, and a count of zero would look like one */
	if ( $IS_FAILURE(l_rsp.mh_sts) )
		{
		char	l_name [64];

		close(l_fd);

		s_batch$condname(l_rsp.mh_sts, l_name, sizeof(l_name));

		return	$PUTMSG_FAO(BATCH$_SUBREJECT, l_name, l_rsp.mh_sts);
		}

	if ( (!l_rsp.mh_count) || ($IS_FAILURE(util$read_exact(l_fd, &l_st, sizeof(l_st)))) )
		{
		close(l_fd);

		return	$PUTMSG_FAO(BATCH$_RECVERR, "the reply to SHOW/MANAGER");
		}

	close(l_fd);

	util$fmt_time(l_st.ms_stime, l_when, sizeof(l_when));

	l_elp = (long long) time(NULL) - l_st.ms_stime;

	util$mkfspec(l_up, sizeof(l_up), "!UL !AZ, !2ZL:!2ZL:!2ZL",
			(const fao_prm_t []) { (fao_prm_t) (l_elp / 86400),
						(fao_prm_t) ((l_elp / 86400 == 1) ? "day" : "days"),
						(fao_prm_t) ((l_elp % 86400) / 3600),
						(fao_prm_t) ((l_elp % 3600) / 60),
						(fao_prm_t) (l_elp % 60) }, 5);

	/*
	**  /QUEUES asks after the queues alone. They are carried by the same reply as the state of the
	**  controller, so the two cannot disagree, and the qualifier chooses which of them is shown.
	*/
	if ( a_ctx->cc_queues )
		{
		util$put("\n Queue                Limit  Exec  Pend  Hold  State\n", NULL, 0);
		util$put(" !20*- !5*- !5*- !5*- !5*- !8*-\n", NULL, 0);

		for ( unsigned q = 0; q < l_st.ms_quecnt; q++ )
			{
			util$put(" !20<!AZ!> !5UL !5UL !5UL !5UL  !AZ\n",
					(const fao_prm_t []) { (fao_prm_t) l_st.ms_queues[q].qs_name,
								(fao_prm_t) l_st.ms_queues[q].qs_joblim,
								(fao_prm_t) l_st.ms_queues[q].qs_executing,
								(fao_prm_t) l_st.ms_queues[q].qs_pending,
								(fao_prm_t) l_st.ms_queues[q].qs_holding,
								(fao_prm_t) ((l_st.ms_queues[q].qs_paused)
										? "paused" : "open") }, 6);
			}

		util$put("\nTotal queues: !UL\n", (const fao_prm_t []) { (fao_prm_t) l_st.ms_quecnt }, 1);

		return	BATCH$_NORMAL;
		}

	util$put("\nJob controller of queue !AZ\n", (const fao_prm_t []) { (fao_prm_t) l_st.ms_queue }, 1);

	util$put("    Image ............ BATCH !AZ (!AZ)\n",
			(const fao_prm_t []) { (fao_prm_t) l_st.ms_version, (fao_prm_t) l_st.ms_ident }, 2);
	util$put("    Process .......... pid !UL, running since !AZ (!AZ)\n",
			(const fao_prm_t []) { (fao_prm_t) l_st.ms_pid, (fao_prm_t) l_when,
						(fao_prm_t) l_up }, 3);
	util$put("    Root directory ... !AZ\n", (const fao_prm_t []) { (fao_prm_t) l_st.ms_root }, 1);
	util$put("    Job limit ........ !UL, !UL executing\n",
			(const fao_prm_t []) { (fao_prm_t) l_st.ms_joblim, (fao_prm_t) l_st.ms_executing }, 2);
	util$put("    Queue ............ !UL pending, !UL holding, !UL aborting, !UL retained\n",
			(const fao_prm_t []) { (fao_prm_t) l_st.ms_pending, (fao_prm_t) l_st.ms_holding,
						(fao_prm_t) l_st.ms_aborting, (fao_prm_t) l_st.ms_retained }, 4);
	util$put("    Next entry ....... !UL\n", (const fao_prm_t []) { (fao_prm_t) l_st.ms_entry }, 1);
	util$put("    Logs kept ........ !UL day!AZ\n",
			(const fao_prm_t []) { (fao_prm_t) l_st.ms_logkeep,
						(fao_prm_t) ((l_st.ms_logkeep == 1) ? "" : "s") }, 2);

	if ( l_st.ms_lcorecnt )
		util$put("    Processors ....... !AZ, !UL in the pool\n",
				(const fao_prm_t []) { (fao_prm_t) l_st.ms_lcore,
							(fao_prm_t) l_st.ms_lcorecnt }, 2);
	else	util$put("    Processors ....... whatever the controller inherited\n", NULL, 0);

	util$put("\n", NULL, 0);

	return	BATCH$_NORMAL;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Examines the queue journal and reports what is wrong with it, optionally displaying what it
**	holds.
**
**	The file is read directly rather than through the job controller, so the command serves where
**	the controller will not start - which is when it is wanted most - and serves equally on a copy
**	of a journal taken from elsewhere.
**
**	Nothing is written. A journal found to be damaged is left as it stands: what to do about it is
**	a decision for whoever ran the command, and a repair made without being asked for would be the
**	worse surprise.
**
**  INPUT PARAMETERS:
**	a_spec	- the journal, an empty string meaning the one of this subsystem.
**	a_ctx	- the parsed command line, not NULL.
**
**  OUTPUT PARAMETERS:
**	None; the report is written to the standard output.
**
**  RETURNS:
**	BATCH$_NORMAL where the journal is sound, BATCH$_JRNCORRUPT where it is not.
**--
*/
static	int	s_batch$do_analyze (
			const char *		a_spec,
			const BATCH$CLICTX *	a_ctx
			)
{
BATCHD$JRNHDR	l_hdr;
BATCH$JOB	l_job;
unsigned char	l_rec [BATCHJRN$K_RECMAX];
char		l_spec [BATCH$K_PATHSZ], l_when [32];
const char *	l_state;
unsigned int	l_seen [BATCH$K_MAXJOB];
unsigned	l_i, l_n, l_bad = 0, l_reclen, l_nseen = 0;
int		l_fd;

	if ( a_spec[0] )
		util$mkfspec(l_spec, sizeof(l_spec), "!AZ", (const fao_prm_t []) { (fao_prm_t) a_spec }, 1);
	else	util$mkfspec(l_spec, sizeof(l_spec), "!AZ/!AZ",
				(const fao_prm_t []) { (fao_prm_t) g_root, (fao_prm_t) "journal.dat" }, 2);

	if ( 0 > (l_fd = open(l_spec, O_RDONLY)) )
		return	$PUTMSG_FAO(BATCH$_OPENIN, l_spec, -1, errno, strerror(errno));

	util$put("Analysis of !AZ\n\n", (const fao_prm_t []) { (fao_prm_t) l_spec }, 1);

	if ( $IS_FAILURE(util$read_exact(l_fd, &l_hdr, sizeof(l_hdr))) )
		{
		close(l_fd);

		util$put("  the header is short: the file is not a queue journal\n", NULL, 0);

		return	BATCH$_JRNCORRUPT;
		}

	util$put("  Signature ........ !XL!AZ\n", (const fao_prm_t []) { (fao_prm_t) l_hdr.jh_magic,
			(fao_prm_t) ((l_hdr.jh_magic == IPC$K_MAGIC) ? "" : "  <- not that of this subsystem") }, 2);

	util$put("  Format ........... !UL!AZ\n", (const fao_prm_t []) { (fao_prm_t) l_hdr.jh_version,
			(fao_prm_t) ((l_hdr.jh_version == BATCHJRN$K_VERSION) ? ""
					: "  <- not the format this revision writes") }, 2);

	util$put("  Records .......... !UL\n", (const fao_prm_t []) { (fao_prm_t) l_hdr.jh_count }, 1);
	util$put("  Entry counter .... !UL\n\n", (const fao_prm_t []) { (fao_prm_t) l_hdr.jh_entry }, 1);

	if ( (l_hdr.jh_magic != IPC$K_MAGIC) || (l_hdr.jh_version != BATCHJRN$K_VERSION) )
		{
		close(l_fd);

		return	BATCH$_JRNCORRUPT;
		}

	for ( l_n = 0; l_n < l_hdr.jh_count; l_n++ )
		{
		l_reclen = 0;

		if ( $IS_FAILURE(util$read_exact(l_fd, &l_reclen, sizeof(l_reclen))) )
			{
			util$put("  record !UL of !UL: the file ends here\n",
					(const fao_prm_t []) { (fao_prm_t) (l_n + 1), (fao_prm_t) l_hdr.jh_count }, 2);
			l_bad++;
			break;
			}

		if ( (!l_reclen) || (l_reclen > sizeof(l_rec)) )
			{
			util$put("  record !UL: length !UL is not credible\n",
					(const fao_prm_t []) { (fao_prm_t) (l_n + 1), (fao_prm_t) l_reclen }, 2);
			l_bad++;
			break;
			}

		if ( $IS_FAILURE(util$read_exact(l_fd, l_rec, l_reclen)) )
			{
			util$put("  record !UL: only part of it is there\n",
					(const fao_prm_t []) { (fao_prm_t) (l_n + 1) }, 1);
			l_bad++;
			break;
			}

		if ( $IS_FAILURE(util$job_decode(l_rec, l_reclen, &l_job)) )
			{
			util$put("  record !UL: cannot be decoded\n", (const fao_prm_t []) { (fao_prm_t) (l_n + 1) }, 1);
			l_bad++;
			continue;
			}

		/*
		**  The checks are of the kind a reader of the queue depends upon: an entry number that is
		**  there and unique, a state that names a state, a script to run and a directory to run it
		**  in. A field merely odd - a priority out of range, a time in the past - is not reported:
		**  the queue would still work, and a report that cries at everything is soon not read.
		*/
		if ( !l_job.jb_entry )
			{
			util$put("  record !UL: no entry number\n", (const fao_prm_t []) { (fao_prm_t) (l_n + 1) }, 1);
			l_bad++;
			}

		for ( l_i = 0; l_i < l_nseen; l_i++ )
			{
			if ( l_seen[l_i] == l_job.jb_entry )
				{
				util$put("  record !UL: entry !UL occurs more than once\n",
					(const fao_prm_t []) { (fao_prm_t) (l_n + 1), (fao_prm_t) l_job.jb_entry }, 2);
				l_bad++;
				break;
				}
			}

		if ( l_nseen < BATCH$K_MAXJOB )
			l_seen[l_nseen++] = l_job.jb_entry;

		if ( l_job.jb_entry > l_hdr.jh_entry )
			{
			util$put("  record !UL: entry !UL is above the counter !UL, so the number could be issued twice\n",
					(const fao_prm_t []) { (fao_prm_t) (l_n + 1), (fao_prm_t) l_job.jb_entry,
								(fao_prm_t) l_hdr.jh_entry }, 3);
			l_bad++;
			}

		if ( (l_job.jb_state < JOB$K_PENDING) || (l_job.jb_state > JOB$K_RETAINED) )
			{
			util$put("  entry !UL: state !UL names no state\n",
					(const fao_prm_t []) { (fao_prm_t) l_job.jb_entry, (fao_prm_t) l_job.jb_state }, 2);
			l_bad++;
			}

		if ( l_job.jb_script[0] != '/' )
			{
			util$put("  entry !UL: the script is not an absolute path\n",
					(const fao_prm_t []) { (fao_prm_t) l_job.jb_entry }, 1);
			l_bad++;
			}

		if ( l_job.jb_cwd[0] != '/' )
			{
			util$put("  entry !UL: the working directory is not an absolute path\n",
					(const fao_prm_t []) { (fao_prm_t) l_job.jb_entry }, 1);
			l_bad++;
			}

		/*
		**  Tags this revision does not assign. Their presence is not damage - a reader passes
		**  over what it does not know, which is what lets a journal cross a revision - but it is
		**  worth saying, since a journal written by a later revision and a journal quietly
		**  corrupted look alike until the tags are named. The value is shown as octets: what it
		**  means is not known here, and rendering it as a number would be a guess.
		*/
		s_batch$unknown_tags(l_rec, l_reclen, l_job.jb_entry);

		if ( !a_ctx->cc_dump )
			continue;

		util$fmt_time(l_job.jb_qtime, l_when, sizeof(l_when));
		util$state_name(l_job.jb_state, &l_state);

		/*
		**  Every label is followed by a colon and every value stands apart from it. A dump is read
		**  by eye and by script alike, and a line whose parts are separated only by spaces obliges
		**  both to count words.
		*/
		util$put("\n  Entry: !UL, job: !AZ, !UL octets\n", (const fao_prm_t []) { (fao_prm_t) l_job.jb_entry,
					(fao_prm_t) l_job.jb_name, (fao_prm_t) l_reclen }, 3);
		util$put("    State: !AZ, owner: [!UL:!UL], priority: !UL\n",
					(const fao_prm_t []) { (fao_prm_t) l_state,
								(fao_prm_t) l_job.jb_uid, (fao_prm_t) l_job.jb_gid,
								(fao_prm_t) l_job.jb_prio }, 4);
		util$put("    Queued: !AZ, queue: !AZ\n", (const fao_prm_t []) { (fao_prm_t) l_when,
								(fao_prm_t) l_job.jb_queue }, 2);
		util$put("    Script: !AZ\n", (const fao_prm_t []) { (fao_prm_t) l_job.jb_script }, 1);

		/*
		**  How it ended, for an entry that has ended. A retained entry is retained because the job
		**  failed, and how it failed is the one thing the reader of a dump is after - it was the
		**  one thing the dump did not say.
		*/
		if ( l_job.jb_state == JOB$K_RETAINED )
			{
			util$fmt_time(l_job.jb_etime, l_when, sizeof(l_when));

			if ( l_job.jb_signal )
				util$put("    Ended: !AZ, signal: !SL\n",
						(const fao_prm_t []) { (fao_prm_t) l_when,
									(fao_prm_t) l_job.jb_signal }, 2);
			else	util$put("    Ended: !AZ, exit: !SL\n",
						(const fao_prm_t []) { (fao_prm_t) l_when,
									(fao_prm_t) l_job.jb_exit }, 2);
			}

		/* And what it waits for, for one that has not begun */
		if ( (l_job.jb_state == JOB$K_HOLDING) && (l_job.jb_after) )
			{
			util$fmt_time(l_job.jb_after, l_when, sizeof(l_when));

			util$put("    Not before: !AZ\n", (const fao_prm_t []) { (fao_prm_t) l_when }, 1);
			}

		if ( l_job.jb_logfile[0] )
			util$put("    Log: !AZ\n", (const fao_prm_t []) { (fao_prm_t) l_job.jb_logfile }, 1);
		}

	close(l_fd);

	util$put("\n", NULL, 0);

	if ( l_bad )
		{
		util$put("!UL fault!%S found in !UL record!%S\n",
				(const fao_prm_t []) { (fao_prm_t) l_bad, (fao_prm_t) l_n }, 2);

		return	BATCH$_JRNCORRUPT;
		}

	util$put("!UL record!%S, no fault found\n", (const fao_prm_t []) { (fao_prm_t) l_n }, 1);

	return	BATCH$_NORMAL;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Walks a run of topic names down the help library from one topic.
**
**  INPUT PARAMETERS:
**	a_lib	  - the open library.
**	a_start	  - the topic to start under, -1 for the top of the library.
**	a_wordcnt - number of topic names.
**	a_words	  - the topic names.
**
**  OUTPUT PARAMETERS:
**	a_index	  - the topic the last name found reached.
**
**  RETURNS:
**	The number of names consumed; a_wordcnt when the whole run was found.
**--
*/
static	int	s_batch$help_walk (
			HLB$LIB *	a_lib,
			int		a_start,
			int		a_wordcnt,
			char **		a_words,
			int *		a_index
			)
{
int	l_i, l_used = 0, l_parent;

	*a_index = a_start;

	/*
	**  A key may carry blanks, so a run of words is offered at once and the routine says how many
	**  of them it took; what remains names a subtopic of what was found.
	*/
	for ( l_i = 0, l_parent = a_start; l_i < a_wordcnt; l_i += l_used, l_parent = *a_index )
		{
		if ( $IS_FAILURE(hlb$lookup_run(a_lib, l_parent, a_wordcnt - l_i, &a_words[l_i], &l_used, a_index)) )
			break;

		if ( !l_used )
			l_used = 1;
		}

	return	l_i;
}

/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Displays a topic of the help library of this subsystem.
**
**	The library is read through the API of the HELP facility rather than by spawning its image:
**	one process is one process, the diagnostics stay ours, and the subsystem answers for its own
**	documentation even where HELP itself was not installed.
**
**	The library is looked for beside the images, then where the software was built to install it,
**	then in the current directory - which is what lets the tests exercise the command out of a
**	build tree that has not been installed.
**
**	The library is rooted at the facility key BATCH, so the topics named are looked for under it
**	first, and under the top of the library after that: an older library spilled its verbs into the
**	top level, and HELP BATCH SUBMIT names the full path.
**
**  INPUT PARAMETERS:
**	a_wordcnt - number of topic names, zero for the whole of the library.
**	a_words	  - the topic names.
**
**  OUTPUT PARAMETERS:
**	None; the text is written to the standard output.
**
**  RETURNS:
**	BATCH$_NORMAL, or BATCH$_NOSCRIPT if the library cannot be read.
**--
*/
static	int	s_batch$do_help (
			int	a_wordcnt,
			char **	a_words
			)
{
static const char *	s_where [] = { BATCH$K_HELPSPEC, "batch.hlb", NULL };

HLB$LIB *	l_lib = NULL;
HELP$OUT	l_out;
const char *	l_txt, *l_spec = NULL;
unsigned	l_len = 0, l_width = 80;
int		l_index = -1, l_i, l_null, l_keep, l_root = -1;

	/*
	**  The diagnostics of the facility go to the standard error, and while the places are being
	**  tried they are not wanted at all: a library absent from the first place is not an error
	**  until it is absent from the last.
	*/
	help$log_init();

	l_null = open("/dev/null", O_WRONLY);
	l_keep = (0 > l_null) ? -1 : dup(STDERR_FILENO);

	if ( l_keep >= 0 )
		dup2(l_null, STDERR_FILENO);

	for ( l_i = 0; s_where[l_i]; l_i++ )
		{
		if ( $IS_SUCCESS(hlb$open(s_where[l_i], &l_lib)) )
			{
			l_spec = s_where[l_i];
			break;
			}
		}

	if ( l_keep >= 0 )
		{
		dup2(l_keep, STDERR_FILENO);
		close(l_keep);
		}

	if ( l_null >= 0 )
		close(l_null);

	if ( !l_spec )
		return	$PUTMSG_FAO(BATCH$_NOSCRIPT, BATCH$K_HELPSPEC, "not found", "hlb$open", -1, ENOENT,
					strerror(ENOENT));

	help$getwinsz(STDOUT_FILENO, &l_width, NULL);
	help$out_init(&l_out, STDOUT_FILENO, STDIN_FILENO, 0, l_width);

	/* The facility key the library is rooted at - its first topic */
	if ( $IS_FAILURE(hlb$firstchild(l_lib, -1, &l_root)) )
		l_root = -1;

	/* No topic named: the root of the library, which is the whole of it */
	if ( a_wordcnt )
		{
		/* Under the facility key first, then from the top of the library (an older one, or BATCH named) */
		l_i = s_batch$help_walk(l_lib, l_root, a_wordcnt, a_words, &l_index);

		if ( (l_i < a_wordcnt) && (l_root != -1) )
			{
		int	l_j = s_batch$help_walk(l_lib, -1, a_wordcnt, a_words, &l_index);

			if ( l_j < a_wordcnt )
				l_i = (l_j > l_i) ? l_j : l_i;		/* the furthest either went names the word at fault */
			else	l_i = l_j;
			}

		if ( l_i < a_wordcnt )
			{
			help$flush(&l_out);
			hlb$close(l_lib);

			return	$PUTMSG_FAO(BATCH$_NOSUCHTOPIC, a_words[l_i]);
			}
		}
	else	l_index = l_root;

	if ( $IS_SUCCESS(hlb$gettext(l_lib, l_index, &l_len, &l_txt)) )
		help$put_text(&l_out, 2, l_len, l_txt);

	help$flush(&l_out);
	hlb$close(l_lib);

	return	BATCH$_NORMAL;
}

/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Entry point of the utility BATCH. It rebuilds the argument vector into the shape the CLI
**	services expect, has them parse it, draws the values out of the parsed command line and passes
**	control to the handler of the command given.
**
**	The CLI services are handed the verb and the qualifiers alone. The parameter is kept back:
**	those services take a declared parameter from the head of the vector without regard to what
**	stands there, and they refuse an argument that looks like a qualifier and names none - which a
**	file specification does. Keeping it back settles both, and a mistyped qualifier still reaches
**	them and is named in their diagnostic, where formerly it arrived as a stray parameter.
**
**  INPUT PARAMETERS:
**	a_argc	- number of command line arguments.
**	a_argv	- vector of arguments: a_argv[1] is the command, followed in any order by the command
**		  parameter and the qualifiers.
**
**  OUTPUT PARAMETERS:
**	None.
**
**  RETURNS:
**	EXIT_SUCCESS if the operation completed, otherwise EXIT_FAILURE.
**--
*/
int	main (
		int	a_argc,
		char **	a_argv
		)
{
BATCH$CLICTX	l_ctx;
CLI_CTX *	l_clictx = NULL;
const char *	l_env, *l_extra = NULL;
char **		l_argv;
char		l_script [BATCH$K_PATHSZ], l_entlist [BATCH$K_CPUSZ] = {0};
int		l_i, l_np, l_nq, l_sts, l_isqual, l_verb, l_isset, l_subv;

	memset(&l_ctx, 0, sizeof(l_ctx));
	l_script[0]	= '\0';
	l_ctx.cc_retain	= RETAIN$K_ERROR;

	if ( $IS_FAILURE(msg$init()) )
		return	EXIT_FAILURE;

	l_env = getenv("BATCH_ROOT");

	if ( l_env )
		util$mkfspec(g_root, sizeof(g_root), "!AZ", (const fao_prm_t []) { (fao_prm_t) l_env }, 1);

	if ( a_argc < 2 )
		{
		s_batch$usage(a_argv[0]);

		return	EXIT_FAILURE;
		}

	/*
	**  HELP answers before the command line is parsed at all. Its parameters are topic names,
	**  spelled however the documentation spells them - /AFTER is a subtopic of SUBMIT - so the CLI
	**  services would refuse them as qualifiers, and the verb itself is not one of theirs.
	**
	**  Being outside their table, it is matched here, and by the rule they apply to the verbs that
	**  are in it: an unambiguous leading portion. HELP shares no leading letter with SUBMIT, SHOW,
	**  DELETE or RELEASE, so any part of it is unambiguous.
	*/
	l_i = (int) strlen(a_argv[1]);

	if ( (l_i) && (!strncasecmp(a_argv[1], "HELP", (size_t) l_i)) && (l_i <= 4) )
		{
		l_sts = s_batch$do_help(a_argc - 2, &a_argv[2]);

		return	($IS_SUCCESS(l_sts)) ? EXIT_SUCCESS : EXIT_FAILURE;
		}

	if ( !(l_argv = calloc((size_t) a_argc, sizeof(char *))) )
		return	EXIT_FAILURE;

	/* The verb, then everything that is not a qualifier, then everything that is */
	l_argv[0]	= a_argv[1];
	l_np		= 0;
	l_nq		= 1;

	/*
	**  SET is a verb of two words. The CLI services look the second up in the sub-verb table of the
	**  first and expect it right after it, whereas the classification below would take it for the
	**  parameter, so the first argument that is not a qualifier is put back where they want it. The
	**  verb is recognized here by the rule they apply - an unambiguous leading portion, which for SET
	**  is SE or SET, S alone being shared with SHOW, SUBMIT and SYNC - and a word that is not SET
	**  passes untouched.
	*/
	l_i	= (int) strlen(a_argv[1]);
	l_isset	= ((l_i >= 2) && (l_i <= 3) && (!strncasecmp(a_argv[1], "SET", (size_t) l_i)));
	l_subv	= -1;

	for ( l_i = 2; (l_isset) && (l_i < a_argc); l_i++ )
		{
		s_batch$is_qual(a_argv[l_i], &l_isqual);

		if ( l_isqual )
			continue;

		l_argv[l_nq++]	= a_argv[l_i];
		l_subv		= l_i;
		break;
		}

	for ( l_i = 2; l_i < a_argc; l_i++ )
		{
		if ( l_i == l_subv )
			continue;

		s_batch$is_qual(a_argv[l_i], &l_isqual);

		if ( l_isqual )
			{
			l_argv[l_nq++] = a_argv[l_i];
			continue;
			}

		if ( !(l_np++) )
			util$mkfspec(l_script, sizeof(l_script), "!AZ",
					(const fao_prm_t []) { (fao_prm_t) a_argv[l_i] }, 1);
		else	l_extra = a_argv[l_i];
		}

	/* Every verb of this command takes at most one parameter */
	if ( l_np > 1 )
		{
		$PUTMSG_FAO(BATCH$_EXTRAPARM, l_extra);

		return	EXIT_FAILURE;
		}

	if ( $IS_FAILURE(__cli$parse(s_verbs, CLI$M_OPSIGNAL, l_nq, l_argv, (void **) &l_clictx)) )
		{
		s_batch$usage(a_argv[0]);

		return	EXIT_FAILURE;
		}

	/*
	**  A qualifier that was not given is not an error here, whereas the signalling option would
	**  have CLI$GET_VALUE report every one of them.
	*/
	l_clictx->opts &= ~CLI$M_OPSIGNAL;

	if ( $IS_FAILURE(s_batch$get_quals(l_clictx, &l_ctx)) )
		return	EXIT_FAILURE;

	/*
	**  The parameter was set aside during the classification above. For DELETE and RELEASE the
	**  entry number may equally have come from /ENTRY, which takes precedence.
	*/
	/*
	**  The list of entries for DELETE and RELEASE: the parameter, or the value of /ENTRY where
	**  that was given, which takes precedence. It is kept as written rather than reduced to a
	**  number here, a list of one being a list all the same.
	*/
	if ( l_ctx.cc_entrystr[0] )
		util$mkfspec(l_entlist, sizeof(l_entlist), "!AZ",
				(const fao_prm_t []) { (fao_prm_t) l_ctx.cc_entrystr }, 1);
	else if ( (l_script[0]) && ((isdigit((unsigned char) l_script[0])) || (l_script[0] == '(')) )
		util$mkfspec(l_entlist, sizeof(l_entlist), "!AZ",
				(const fao_prm_t []) { (fao_prm_t) l_script }, 1);

	if ( (!l_ctx.cc_entry) && (l_entlist[0]) )
		l_ctx.cc_entry = (unsigned int) strtoul(l_entlist, NULL, 10);

	/*
	**  Which verb was chosen is settled by the address of its descriptor. There is no default
	**  branch: a verb the table does not hold cannot reach this point, the services having
	**  refused it, and were one to arrive it would be a defect rather than a request.
	*/
	l_verb = -1;

	for ( l_i = 0; l_i < VERB$K_MAXNR; l_i++ )
		{
		if ( (l_clictx->vlist) && (l_clictx->vlist->verb == &s_verbs[l_i]) )
			{
			l_verb = l_i;
			break;
			}
		}

	switch ( l_verb )
		{
		case	VERB$K_SUBMIT:
			if ( !l_script[0] )
				{
				$PUTMSG_FAO(BATCH$_NOPARAM);

				return	EXIT_FAILURE;
				}

			l_sts = s_batch$do_submit(l_script, &l_ctx);
			break;

		case	VERB$K_SHOW:
			l_sts = (l_ctx.cc_manager || l_ctx.cc_queues) ? s_batch$do_manager(&l_ctx)
									: s_batch$do_show(&l_ctx);
			break;

		case	VERB$K_PURGE:
			l_sts = s_batch$do_purge(&l_ctx);
			break;

		case	VERB$K_QUEUE:
			l_sts = s_batch$do_queueop(l_script, &l_ctx);
			break;

		case	VERB$K_DELETE:
			/*
			**  DELETE takes an entry away and signals a job that may be running: it cannot be
			**  undone, and a list makes a slip cost more than one entry. The question is asked
			**  where /CONFIRM asks for it; without the qualifier the command behaves as it always
			**  has, an established command not being one to change the manners of unasked.
			*/
			if ( $MATCH_COND(s_batch$confirm("Delete the entries named", &l_ctx), BATCH$_NOTCONF) )
				{
				l_sts = BATCH$_NOTCONF;
				break;
				}

			l_sts = s_batch$do_entrylist(IPC$K_DELETE, l_entlist, NULL, 0);
			break;

		case	VERB$K_RELEASE:
			l_sts = s_batch$do_entrylist(IPC$K_RELEASE, l_entlist, NULL, 0);
			break;

		case	VERB$K_SET:
			/*
			**  The word after SET is settled as the verb was, by the address of its descriptor
			**  in the sub-verb table. The services refuse a word the table does not hold, so a
			**  second item that is not ENTRY cannot arrive; were one to, it would be a defect.
			*/
			if ( (!l_clictx->vlist->next) || (l_clictx->vlist->next->verb != &s_setwhat[SETWHAT$K_ENTRY]) )
				{
				l_sts = $PUTMSG_FAO(BATCH$_BUGCHECK, "the parsed sub-verb of SET is not one of the table");
				break;
				}

			l_sts = s_batch$do_setentry(l_entlist, &l_ctx);
			break;

		case	VERB$K_ANALYZE:
			l_sts = s_batch$do_analyze(l_script, &l_ctx);
			break;

		case	VERB$K_SYNC:
			if ( !l_ctx.cc_entry )
				{
				$PUTMSG_FAO(BATCH$_NOENTRY);

				return	EXIT_FAILURE;
				}

			l_sts = s_batch$do_sync(&l_ctx);
			break;

		default:
			l_sts = $PUTMSG_FAO(BATCH$_BUGCHECK, "the parsed verb is not one of the table");
			break;
		}

	__cli$cleanup(l_clictx);
	free(l_argv);

	return	($IS_SUCCESS(l_sts)) ? EXIT_SUCCESS : EXIT_FAILURE;
}
