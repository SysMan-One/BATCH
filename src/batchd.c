/*
**++
**  MODULE:		BATCHD.C
**  IDENT:		X-02-02
**  REV:		02.02.00
**
**  FACILITY:		BATCH
**
**  ABSTRACT:		Job controller of the batch execution subsystem. It owns the job queue, schedules
**			jobs with regard to the limit on simultaneously executing jobs, to priority and
**			to the /AFTER time, creates the command interpreter process, redirects its output
**			to the job log, tracks termination and services requests from the utility BATCH.
**
**			The queue is held in a table of fixed size and, after every change, is written to
**			the journal in its entirety by way of a temporary file followed by rename(2),
**			which makes the write atomic and lets the queue survive a restart of the daemon.
**
**  AUTHOR:		StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
**
**  CREATION DATE:	28-JUL-2026
**
**  MODIFICATION HISTORY:
**
**	28-JUL-2026	RRL	X-01-01	Initial revision. One queue, a job limit, the states PENDING, HOLDING,
**				EXECUTING, ABORTING and RETAINED. Sender identification by SO_PEERCRED.
**
**	28-JUL-2026	RRL	X-01-02	The command interpreter specification moved to BATCH$K_SHELL. File
**				specification assembly converted to UTIL$MKFSPEC.
**
**	28-JUL-2026	RRL	X-01-03	Conformance to the coding rules: every routine returns a status, values
**				are delivered through output parameters, braces removed from single
**				statements under if, type BATCH_JOB_t renamed to BATCH$JOB.
**
**	28-JUL-2026	RRL	X-01-04	Routines return condition values of the facility from BATCHMSG.H instead
**				of generic severity codes. The protocol header was renamed to BATCHIPC.H
**				and its function constants to IPC$K_*.
**
**	28-JUL-2026	RRL	X-01-05	Comments and message texts converted to English.
**
**	28-JUL-2026	RRL	X-01-06	The command line is parsed by the CLI services of UTILITY$ROUTINES
**				instead of getopt(3). The options accordingly acquired the DCL form,
**				/ROOT=, /JOB_LIMIT=, /QUEUE=, /FOREGROUND and /VERBOSE, and a value is
**				now attached to its qualifier by an equals sign rather than separated by
**				a blank. The single-letter abbreviations remain unambiguous, so -r=, -j=,
**				-q=, -f and -v continue to be accepted.
**
**	28-JUL-2026	RRL	X-01-07	Static routines were given the prefix s_ as the coding rules require.
**
**	28-JUL-2026	RRL	X-01-08	The result of every C RTL routine is captured at the point of the call and
**				reported together with errno.
**
**	28-JUL-2026	RRL	X-01-09	Formatting converted to the FAO engine of STARLET; the dependence on
**				<stdio.h> was withdrawn.
**
**	29-JUL-2026	RRL	X-01-10	The job table of fixed size gave way to the queue of UTILITY$ROUTINES.
**				An entry is allocated when a job is placed in the queue and released when
**				it leaves, so the controller holds what its jobs need rather than close to
**				two megabytes whether it has jobs or not. The journal is unaffected: the
**				links live in an entry that wraps the descriptor, and the descriptor alone
**				is written. The rule of selection for SHOW moved into a routine of its own,
**				the queue being walked twice - once to count, once to transmit.
**
**	29-JUL-2026	RRL	X-01-11	The /RESTART qualifier is honoured: a job so submitted is put back in the
**				queue when the controller starts again, instead of being kept as orphaned.
**				The job is told of it through BATCH_RESTART and BATCH_RESTARTS.
**
**	29-JUL-2026	RRL	X-01-12	Connections are served without blocking. Every open connection is awaited
**				alongside the listening socket and the self-pipe, and is advanced a step at
**				a time as its descriptor becomes ready; what was local to S_BATCHD$SERVE is
**				now kept in BATCHD$CONN between the steps. A reply is assembled in memory
**				before any of it is transmitted, so the queue is not held open across a
**				transfer to a slow reader, and a connection that makes no progress within
**				BATCHD$K_CONNTMO seconds is dropped.
**
**	29-JUL-2026	RRL	X-01-13	The /CPU qualifier is honoured: the affinity is established in the created
**				process, before it assumes the identity of the submitter. The mask the
**				controller itself runs under is reported at the start, since a job that
**				names none inherits it. The log directory is swept of what belongs to no
**				entry, /LOG_RETAIN governing how long a file left behind is kept.
**
**	29-JUL-2026	RRL	X-01-14	A change made by a request was not journalled. The main loop tested the
**				state of a connection after the step rather than before it, and a step that
**				read a request and acted upon it left the connection writing its reply, so
**				the indication that the queue had changed was discarded. A change that the
**				scheduler did not follow with one of its own - a job placed with /HOLD, or
**				an entry deleted - was then lost when the controller stopped.
**
**	30-JUL-2026	RRL	X-01-15	The CLI services are called by their names in the revision now vendored,
**				which carry the __cli$ prefix.
**
**	 1-AUG-2026	RRL	X-01-16	Detaching was done first of all, so a controller that could not start said
**				nothing on the terminal and confided the reason to a log the reader did not
**				yet know about; the command returned in silence whether it had started or
**				not. It is now done last, after the root has been made, the journal read
**				and the socket bound, and the line stating that it started is issued before
**				it. The log of the controller is opened at the same moment.
**
**				The socket of a previous run was removed without asking whether anyone was
**				listening on it, so a second controller took the root away from a living one
**				and the two wrote one journal in turn. A living socket is now detected and
**				the second controller refuses to start.
**
**	 2-AUG-2026	RRL	X-01-17	The log of a job is written by default in the directory it was submitted
**				from, as it is in OpenVMS, the log directory of the subsystem being the
**				fallback where the submitter may not write there. Whether he may is decided
**				by the utility, which runs as he does.
**
**				The log is now opened before the identity is assumed and made over to the
**				submitter with FCHOWN. Opened after, it could not be created in a log
**				directory belonging to the privileged user, and a job of an ordinary user
**				failed with BATCHD$K_EXECFAIL before its first line - having nowhere to say
**				why, the log being the thing that could not be made.
**
**	 2-AUG-2026	RRL	X-01-18	A record of the queue journal describes itself: it is a sequence of tagged
**				values rather than the image of BATCH$JOB, so a journal outlives the layout
**				it was written by and crosses a revision in either direction. The header now
**				states the format of the journal rather than the size of the descriptor, and
**				a record that will not decode is passed over rather than ending the load.
**
**	 2-AUG-2026	RRL	X-01-19	The log of the controller is opened before anything is issued, so it holds
**				the whole of a run rather than the part following the detaching - which is
**				what X-16 left it with, the identification and the loading of the journal
**				going to the terminal and nowhere else. UTIL$SETLOGFD points the logging at
**				the file without seizing the streams, which are still wanted for the summary
**				now issued on the terminal before it is let go.
**
**	 2-AUG-2026	RRL	X-01-20	The /USER qualifier: a job may be run as somebody other than the submitter,
**				and only the privileged user may ask for it. The identity is settled here
**				rather than by the utility - the utility runs as whoever invoked it and could
**				claim anything, whereas the identity of the sender comes from the kernel.
**
**	 2-AUG-2026	RRL	X-01-21	Every use of /USER is recorded in the log, naming who asked and for whom,
**				and so is every refusal. Running a job as somebody else is the one operation
**				of this subsystem that grants an authority rather than uses one, and a grant
**				that leaves no record cannot be reviewed afterwards.
**
**	 2-AUG-2026	RRL	X-01-22	SHOW no longer filters the privileged user by ownership. He may delete and
**				release any entry, so an entry he cannot see is worse than useless; it became
**				plain with /USER, a job submitted to run as somebody else belonging to that
**				somebody at once, so its submitter could not see the entry he had just been
**				given the number of.
**
**	 2-AUG-2026	RRL	X-01-23	The default job limit is sixteen rather than four, and the /LCORE qualifier
**				confines the batch load to a pool of processors and spreads it over them,
**				one to each job in turn. A job naming /CPU keeps what it named.
**	 2-AUG-2026	RRL	X-01-24	A controller that has been signalled holds its socket until it has finished
**				stopping, and its successor refused to start on finding it there. The socket
**				is now tried for a couple of seconds before the refusal is believed, so that
**				"pkill batchd && batchd" - an ordinary way to restart the thing - works.
**
**	 2-AUG-2026	RRL	X-01-25	Three faults of privilege, found in review:
**
**				A log named by the client was opened, and unlinked, by the privileged user.
**				Any user who could reach the socket could thereby have any file he named
**				created, truncated or taken away. Such a path is now opened after the
**				identity of the job has been assumed, with O_NOFOLLOW, and is never
**				unlinked; a path this controller built is treated as before.
**
**				SETUID leaves the supplementary groups as they were, so a job kept those of
**				the daemon. INITGROUPS is called for the user the job runs as, and where his
**				name cannot be had the groups are emptied outright.
**
**				The script was examined at submission by the controller, which answered a
**				question nobody asked - whether the privileged user could read it - and left
**				room for the file to be replaced in the meantime. It is opened again under
**				the identity that will run it.
**
**	 2-AUG-2026	RRL	X-01-26	From a review of the above, three checks it wanted and this had not:
**
**				The name of a job becomes part of the specification of its log, and that
**				log is the one file opened with privilege. A name of "../../x" led that
**				open out of the log directory; a name is now required to be a name.
**
**				A script, a working directory and a log specification are required to be
**				absolute. A relative one would be resolved against whatever the controller
**				happened to have for a working directory.
**
**				S_BATCHD$UNDER_LOGDIR guards the privileged open itself. The rules above
**				should make it unnecessary; a guard at the act does not depend on every one
**				of them having been remembered.
**
**	 2-AUG-2026	RRL	X-01-27	A script found to have been replaced is refused by the controller, with
**				BATCH$_SCRIPTCHG naming the entry and the file, and the entry is retained
**				so the reason can be read. The child kept its own check, but a child can
**				only exit, and an exit says exit=126 and nothing else. The file is also
**				required to be a regular one at that moment.
**
**	 2-AUG-2026	RRL	X-01-28	Dispositions are installed with SIGACTION rather than SIGNAL, in the
**				controller and in the child of a job alike. What SIGNAL means is not
**				settled between systems, and taken in the Berkeley sense it implies
**				SA_RESTART - which this design does not want: the main loop relies upon
**				POLL returning when a signal arrives. A failure to install is now reported
**				rather than passed over.
**
**	 4-AUG-2026	RRL	X-01-26	An exit status above 128 is reported with the signal it implies. A shell
**				that ends because the command it ran was killed exits with 128 plus the
**				number of the signal; the job did not die of a signal itself, so no other
**				part of the display says so, and 135 is not a thing anyone knows by sight.
**				It is worded as a surmise, a program being free to exit with 135 of its own.
**
**	 5-AUG-2026	RRL	X-01-27	Queues of their own. A queue is a name, a limit and a state; the scheduler
**				fills each in turn and to its own limit, and a paused one is passed over
**				entirely. The table comes from /QUEUE, whose first name is the default: a
**				separate /QUEUES would have been an abbreviation of /QUEUE, and the services
**				would have had to guess which was meant. A queue the table does not hold is
**				refused at submission.
**
**	 5-AUG-2026	RRL	X-01-28	Three ways a queue could mislead, closed:
**
**				An entry whose queue the table no longer holds waited for ever and looked
**				exactly like an entry waiting its turn. Such a queue is now restored at the
**				load, and restored paused, so the entry stands still for a reason that can
**				be read.
**
**				The limits and the paused states are kept in QUEUES.DAT beside the journal -
**				beside it and not within it, the journal declaring itself a sequence of job
**				records. A queue paused on purpose and found open after a restart would be
**				a trap rather than a fault.
**
**				The queue built into the subsystem is always in the table. Without this a
**				controller told /QUEUE=slow had no SYS$BATCH at all, and every entry left
**				in it by a previous run was stranded.
**
**	20-AUG-2026	RRL	X-01-29	IPC$K_PURGE removes the entries of jobs that have finished. Only an
**				entry in the RETAINED state is taken - nothing else in the queue is finished
**				with - and the entries of others are passed over unless the caller is the
**				privileged user.
**
**	28-AUG-2026	RRL	X-01-30	Diagnostics are issued by $PUTMSG_FAO. The message table is written for
**				the FAO engine, and the envelope %FAC-S-IDENT is supplied by the library
**				rather than carried in every text - a text that carried its own was framed
**				twice. The parameters are unchanged at the point of call: $PUTMSG_FAO casts
**				each to FAO_PRM_T itself and derives their count from the list.
**
**	 8-SEP-2026	RRL	X-01-31	IPC$K_SETENTRY alters the attributes of an entry in place - priority, start
**				time, holding, name, retention, log keeping, restart and processor list -
**				for the SET ENTRY command. What decides how and when a job starts may be
**				altered while it waits and not once it runs; what decides what becomes of
**				it afterwards may be altered until it is over. A retained entry is not
**				altered at all: nothing about it is still to be decided.
**
**	 8-OCT-2026	RRL	X-01-32	SHOW without /ALL displays the jobs that wait as well - PENDING and HOLDING
**				- and leaves out only the entry kept after a job that succeeded, which /ALL
**				adds. A job held until next week had been invisible to an ordinary SHOW, and
**				a queue that seemed to have lost its weekly job had it all the while.
**
**	18-OCT-2026	RRL	X-02-02	Revision raised to X-02-02 together with the product; no change of its own.
**--
*/

#include	<errno.h>
#include	<fcntl.h>
#include	<dirent.h>
#include	<poll.h>
#include	<signal.h>
#include	<stdlib.h>
#include	<string.h>
#include	<unistd.h>
#include	<pwd.h>
#include	<grp.h>
#include	<sys/socket.h>
#include	<sys/stat.h>
#include	<sys/types.h>
#include	<sys/un.h>
#include	<sys/wait.h>

#include	"batchdef.h"
#include	"batchipc.h"
#include	"batchjrn.h"
/*
**  The CLI header is not self-contained: it declares the action routine of a verb as
**  int (*act_rtn) (__unknown_params), while __unknown_params itself is defined in CLI_ROUTINES.C
**  and not in the header. Left undefined, the declaration degenerates into the obsolete form with a
**  named parameter of no type. The definition below repeats the one the library makes for C, so
**  that the type of the member is the same in every translation unit.
*/
#define	__unknown_params

#include	"cli_routines.h"

#undef	__MODULE__
#undef	__IDENT__
#undef	__REV__

#define	__MODULE__	"BATCHD"
#define	__IDENT__	"X-02-02"
#define	__REV__		"02.02.00"

#define	BATCHD$K_JRNNAME	"journal.dat"		/* File name of the queue journal 					*/
#define	BATCHD$K_QUENAME	"queues.dat"		/* File the states of the queues are kept in 				*/
#define	BATCHD$K_JRNTMP		"journal.tmp"		/* File name of the temporary journal 					*/
#define	BATCHD$K_INUSETRY	20			/* Attempts to reach a controller before it is believed gone	*/
#define	BATCHD$K_INUSEWAIT	100000			/* Pause between those attempts, microseconds			*/
#define	BATCHD$K_LOGDIR		"log"			/* Name of the job log subdirectory 					*/
#define	BATCHD$K_POLLMS		1000			/* Upper bound on the wait in poll(2), milliseconds 			*/
#define	BATCHD$K_LOGNAME	"batchd.log" /* File name of the job controller log */
#define	BATCHD$K_LOGMAX		(1024 * 1024) /* Size at which the log is rewound, bytes */
#define	BATCHD$K_LOGKEEP	7 /* Days a job log left behind is kept, zero disables the sweep */
#define	BATCHD$K_SWEEPSEC	3600 /* Seconds between one sweep of the log directory and the next */
#define	BATCHD$K_MAXCONN	64 /* Connections served at one time */
#define	BATCHD$K_CONNTMO	30 /* Seconds a connection may make no progress before it is dropped */
#define	BATCHD$K_LISTENQ	16			/* Length of the queue of pending connections 				*/
#define	BATCHD$K_EXECFAIL	126			/* Exit status when a job cannot be started 				*/


/*
**  Command line definition in the CLI convention. The qualifiers are matched by an unambiguous
**  leading portion of their name, and the first letters are all distinct, so a single letter
**  suffices. A value is attached to its qualifier by an equals sign.
*/
enum	{
	BATCHD$K_QROOT = 0,
	BATCHD$K_QJOBLIM,
	BATCHD$K_QQUEUE,
	BATCHD$K_QFOREGND,
	BATCHD$K_QVERBOSE,
	BATCHD$K_QLOGKEEP,
	BATCHD$K_QLCORE,

	BATCHD$K_QMAXNR
	};

static	CLI_PQDESC	s_quals [BATCHD$K_QMAXNR + 1] =
	{
	{ {$ASCINI("ROOT")},		CLI$K_QSTRING,	CLI$K_QUAL, 0, {0}, NULL },
	{ {$ASCINI("JOB_LIMIT")},	CLI$K_NUM,	CLI$K_QUAL, 0, {0}, NULL },
	{ {$ASCINI("QUEUE")},		CLI$K_QSTRING,	CLI$K_QUAL, 0, {0}, NULL },
	{ {$ASCINI("FOREGROUND")},	CLI$K_OPT,	CLI$K_QUAL, 0, {0}, NULL },
	{ {$ASCINI("VERBOSE")},		CLI$K_OPT,	CLI$K_QUAL, 0, {0}, NULL },
	{ {$ASCINI("LOG_RETAIN")},	CLI$K_NUM,	CLI$K_QUAL, 0, {0}, NULL },
	{ {$ASCINI("LCORE")},		CLI$K_QSTRING,	CLI$K_QUAL, 0, {0}, NULL },
	{ {0},				0,		0,	    0, {0}, NULL }
	};

static	CLI_VERB	s_verbs [] =
	{
	{ {$ASCINI("BATCHD")},	NULL, NULL, s_quals, NULL, NULL },
	{ {0},			NULL, NULL, NULL,    NULL, NULL }
	};

/*
**  A queue entry. The links come first, as the $INSQ and $REMQ macros of UTILITY$ROUTINES require;
**  the descriptor follows them whole, so what is written to the journal is the descriptor alone and
**  the format of the journal is unaffected by the entry that carries it.
*/
typedef struct batchd_jobent_t {
	ENTRY		je_links;
	BATCH$JOB	je_job;
} BATCHD$JOBENT;

/*
**  State of a client connection. A connection is served without blocking, a step at a time as the
**  descriptor becomes ready, so what would otherwise be local variables of one routine has to be
**  kept between the steps.
*/
enum	{
	CONN$K_RDHDR = 0,	/* Reading the frame header */
	CONN$K_RDPLD,		/* Reading the payload */
	CONN$K_WRRSP		/* Writing the reply */
	};

typedef struct batchd_conn_t {
	ENTRY		cn_links;
	BATCH$IPCHDR	cn_hdr;				/* The request header */
	long long	cn_deadline;			/* Time by which progress is expected */
	char *		cn_buf;				/* Payload being read or reply being written */
	unsigned int	cn_len;				/* Bytes the buffer is to hold */
	unsigned int	cn_done;			/* Bytes transferred so far */
	unsigned int	cn_state;			/* One of CONN$K_* */
	int		cn_fd;
	int		cn_uid;				/* Identity of the sender, from SO_PEERCRED */
	int		cn_gid;
} BATCHD$CONN;

/*
**  The queue of jobs and the operating parameters of the job controller. An entry is allocated when
**  a job is placed in the queue and released when its entry leaves it, so what the controller holds
**  is proportional to the jobs it actually has rather than to the ceiling it will not exceed.
*/
static	__QUEUE		g_jobque  = QUEUE_INITIALIZER;
static	__QUEUE		g_connque = QUEUE_INITIALIZER;
static	unsigned int	g_entry		= 1;		/* Queue entry number counter 						*/
static	int		g_joblim	= BATCH$K_DEFJOBLIM;

/*
**  The pool of processors jobs are spread over, from /LCORE, and the one to be given out next. It
**  differs from the /CPU of a job in what it settles: /CPU says where one job is to run, whereas
**  this says where jobs in general are to run, one processor to each in turn - so that a machine
**  whose other work must not be disturbed can have the batch load confined to the cores set aside
**  for it, and spread over them rather than heaped upon the first.
**
**  A job that names /CPU keeps what it named: the particular is not overruled by the general.
*/
static	int		g_lcore [CPU_SETSIZE];
static	int		g_lcorecnt	= 0;
static	int		g_lcorenext	= 0;

/*
**  The queues. A queue is a name, a limit of its own and a state; jobs of one queue run in the order
**  their priorities and entry numbers give, and jobs of different queues run beside one another up
**  to the limit each has been given.
**
**  A queue is not created by a command: it exists because the controller was told of it, and the
**  default one exists always. A name a job asks for and the table does not hold is refused at
**  submission - a job placed in a queue nobody minds would wait for ever, and would look exactly
**  like a job that is merely waiting its turn.
*/
typedef struct batchd_queue_t {
	char		qe_name [BATCH$K_QUESZ];		/* Name, as it is written			*/
	int		qe_joblim;				/* Jobs of this queue that may execute at once	*/
	int		qe_paused;				/* Non-zero: nothing of it is started		*/
} BATCHD$QUEUE;

static	BATCHD$QUEUE	g_queues [BATCH$K_MAXQUE];
static	int		g_quecnt	= 0;
static	char		g_lcorestr [BATCH$K_CPUSZ] = {0};	/* The pool as it was written			*/
static	long long	g_stime		= 0;			/* Time this controller started			*/
static	int		g_logkeep	= BATCHD$K_LOGKEEP; /* Days a job log left behind is kept */
static	int		g_verbose	= 0; /* Issue messages of informational severity */
static	int		g_shutdown	= 0;		/* Set by the handler of SIGTERM and SIGINT 				*/
static	int		g_chldpipe [2]	= { -1, -1 };	/* Self-pipe delivering signals to the main loop 			*/
static	char		s_verbname []	= "BATCHD"; /* Verb handed to the CLI services */
static	char		s_shname []	= "sh";		/* Value of argv[0] of the interpreter created 				*/
static	char		g_root [BATCH$K_PATHSZ]	= BATCH$K_DEFROOT;
static	char		g_queue [BATCH$K_QUESZ]	= BATCH$K_DEFQUEUE;

/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Handler of the signals SIGCHLD, SIGTERM and SIGINT. It performs no substantive work, writing
**	only a single byte to the self-pipe in order to bring the main loop out of poll(2). Everything
**	further is done in the main context, where the choice of routines that may be called is not
**	restricted.
**
**	The signature of the routine is imposed by the system and therefore does not follow the
**	general convention of returning a condition value.
**
**  INPUT PARAMETERS:
**	a_sig	- number of the signal delivered.
**
**  OUTPUT PARAMETERS:
**	None.
**
**  RETURNS:
**	None.
**--
*/
static	void	s_batchd$sighandler (
			int	a_sig
			)
{
ssize_t	l_rc;
int	l_save;
char	l_byte;

	/* errno is restored so as not to corrupt it for the routine that was interrupted */
	l_save	= errno;
	l_byte	= (char) a_sig;

	if ( (a_sig == SIGTERM) || (a_sig == SIGINT) )
		g_shutdown = 1;

	/*
	**  Overflow of the self-pipe is immaterial: what matters is the fact that poll(2) is woken,
	**  not the number of bytes accumulated, so the outcome of the write is deliberately unchecked.
	*/
	if ( g_chldpipe[1] != -1 )
		{
		l_rc = write(g_chldpipe[1], &l_byte, 1);
		(void) l_rc;
		}

	errno = l_save;
}

/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Installs a signal disposition with SIGACTION.
**
**	SIGNAL is not used: what it means is not settled between systems, and where it is taken in the
**	Berkeley sense - which is what this library does - it implies SA_RESTART, so a call interrupted
**	by a signal is resumed rather than returning. The main loop wants the opposite: POLL is to come
**	back when something has happened, and the self-pipe is then drained. It works either way, the
**	pipe being the thing that carries the news, but a wait that is silently resumed for its full
**	term is not what the code says it is.
**
**  INPUT PARAMETERS:
**	a_sig		- the signal.
**	a_handler	- SIG_DFL, SIG_IGN, or the handler.
**
**  OUTPUT PARAMETERS:
**	None.
**
**  RETURNS:
**	Zero, or -1 with errno set.
**--
*/
static	int	s_batchd$sigaction (
			int	a_sig,
			void	(*a_handler)(int)
			)
{
struct sigaction	l_sa;

	memset(&l_sa, 0, sizeof(l_sa));

	l_sa.sa_handler	= a_handler;
	l_sa.sa_flags	= 0;

	sigemptyset(&l_sa.sa_mask);

	return	sigaction(a_sig, &l_sa, NULL);
}

/*
**  Walks the queue of jobs from its head. The links are traversed directly rather than through a
**  routine of the library, which offers removal from either end but no iterator.
*/
#define	$JOBQUE_FIRST()		((BATCHD$JOBENT *) g_jobque.head)
#define	$JOBQUE_NEXT(ent)	((BATCHD$JOBENT *) (ent)->je_links.right)

#define	$CONNQUE_FIRST()	((BATCHD$CONN *) g_connque.head)
#define	$CONNQUE_NEXT(cn)	((BATCHD$CONN *) (cn)->cn_links.right)

/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Allocates an entry and places it at the tail of the queue.
**
**	A ceiling is kept although the queue itself imposes none: an unbounded backlog would let a
**	runaway submitter exhaust memory, and the condition reporting a full queue is more use to the
**	one submitting than the failure of an allocation would be.
**
**  INPUT PARAMETERS:
**	None.
**
**  OUTPUT PARAMETERS:
**	a_ent	- receives the address of the entry, not NULL. On failure NULL is supplied.
**
**  RETURNS:
**	BATCH$_NORMAL, or BATCH$_NOSLOT if the queue is at its ceiling or memory is exhausted.
**--
*/
static	int	s_batchd$job_alloc (
			BATCHD$JOBENT **	a_ent
			)
{
BATCHD$JOBENT *	l_ent;
unsigned	l_count;

	*a_ent = NULL;

	if ( g_jobque.count >= BATCH$K_MAXJOB )
		return	BATCH$_NOSLOT;

	if ( !(l_ent = calloc(1, sizeof(BATCHD$JOBENT))) )
		return	BATCH$_NOSLOT;

	if ( $IS_FAILURE($INSQTAIL(&g_jobque, l_ent, &l_count)) )
		{
		free(l_ent);

		return	BATCH$_NOSLOT;
		}

	*a_ent = l_ent;

	return	BATCH$_NORMAL;
}

/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Takes an entry out of the queue and releases it. The log file of the job is removed unless it
**	was asked to be kept.
**
**  INPUT PARAMETERS:
**	a_ent	- address of the entry, not NULL. It is not to be referred to afterwards.
**
**  OUTPUT PARAMETERS:
**	None.
**
**  RETURNS:
**	BATCH$_NORMAL or BATCH$_BUGCHECK.
**--
*/
static	int	s_batchd$job_free (
			BATCHD$JOBENT *	a_ent
			)
{
unsigned	l_count;

	/*
	**  A log this controller named is removed with its entry, unless /KEEP asked otherwise. A log
	**  the client named is never removed: the unlinking is done by the privileged user, and a path
	**  supplied by a client would let him have any file he could name taken away - which is the
	**  same hole as creating one, opened from the other side.
	*/
	if ( (!a_ent->je_job.jb_keeplog) && (!a_ent->je_job.jb_logclient) )
		unlink(a_ent->je_job.jb_logfile);

	if ( $IS_FAILURE($REMQENT(&g_jobque, a_ent, &l_count)) )
		return	$PUTMSG_FAO(BATCH$_BUGCHECK, "a job entry does not belong to the queue");

	free(a_ent);

	return	BATCH$_NORMAL;
}

/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Locates the entry of a job by its queue entry number.
**
**  INPUT PARAMETERS:
**	a_entry	- queue entry number.
**
**  OUTPUT PARAMETERS:
**	a_ent	- receives the address of the entry, not NULL. If it was not found, NULL is supplied.
**
**  RETURNS:
**	BATCH$_NORMAL if the entry was found, otherwise BATCH$_NOSUCHENT.
**--
*/
static	int	s_batchd$job_by_entry (
			unsigned int		a_entry,
			BATCHD$JOBENT **	a_ent
			)
{
BATCHD$JOBENT *	l_ent;

	*a_ent = NULL;

	for ( l_ent = $JOBQUE_FIRST(); l_ent; l_ent = $JOBQUE_NEXT(l_ent) )
		{
		if ( l_ent->je_job.jb_entry != a_entry )
			continue;

		*a_ent = l_ent;

		return	BATCH$_NORMAL;
		}

	return	BATCH$_NOSUCHENT;
}

/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Locates the entry of an executing or aborting job by its process identifier. Only those two
**	states are examined, since the jb_pid field holds a valid value in them alone.
**
**  INPUT PARAMETERS:
**	a_pid	- process identifier as obtained from waitpid(2).
**
**  OUTPUT PARAMETERS:
**	a_ent	- receives the address of the entry, not NULL. If it was not found, NULL is supplied.
**
**  RETURNS:
**	BATCH$_NORMAL if the job was found, otherwise BATCH$_UNKPROC.
**--
*/
static	int	s_batchd$job_by_pid (
			pid_t			a_pid,
			BATCHD$JOBENT **	a_ent
			)
{
BATCHD$JOBENT *	l_ent;

	*a_ent = NULL;

	for ( l_ent = $JOBQUE_FIRST(); l_ent; l_ent = $JOBQUE_NEXT(l_ent) )
		{
		if ( (l_ent->je_job.jb_state != JOB$K_EXECUTING) && (l_ent->je_job.jb_state != JOB$K_ABORTING) )
			continue;

		if ( l_ent->je_job.jb_pid != (int) a_pid )
			continue;

		*a_ent = l_ent;

		return	BATCH$_NORMAL;
		}

	return	BATCH$_UNKPROC;
}

/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Writes the states of the queues beside the journal.
**
**	What is kept is what an administrator set and would be surprised to lose: the limit of each
**	queue and whether it is paused. A queue paused on purpose and found open after a restart is
**	worse than one that will not pause at all - the second is a fault, the first a trap.
**
**  INPUT PARAMETERS:
**	None.
**
**  OUTPUT PARAMETERS:
**	None.
**
**  RETURNS:
**	BATCH$_NORMAL, or the condition of the failure.
**--
*/
static	int	s_batchd$que_by_name (const char *a_name, BATCHD$QUEUE **a_que);
static	int	s_batchd$que_add (const char *a_name, int a_joblim, int a_paused);

static	int	s_batchd$que_save (void)
{
char	l_spec [BATCH$K_PATHSZ], l_line [BATCH$K_QUESZ + 64];
int	l_fd, l_sts = BATCH$_NORMAL;

	if ( $IS_FAILURE(util$mkfspec(l_spec, sizeof(l_spec), "!AZ/!AZ",
				(const fao_prm_t []) { (fao_prm_t) g_root, (fao_prm_t) BATCHD$K_QUENAME }, 2)) )
		return	BATCH$_LOGSPEC;

	if ( 0 > (l_fd = open(l_spec, O_WRONLY | O_CREAT | O_TRUNC, 0640)) )
		return	$PUTMSG_FAO(BATCH$_OPENOUT, l_spec, -1, errno, strerror(errno));

	for ( int q = 0; ($IS_SUCCESS(l_sts)) && (q < g_quecnt); q++ )
		{
		util$mkfspec(l_line, sizeof(l_line), "!AZ !UL !UL\n",
				(const fao_prm_t []) { (fao_prm_t) g_queues[q].qe_name,
							(fao_prm_t) g_queues[q].qe_joblim,
							(fao_prm_t) g_queues[q].qe_paused }, 3);

		l_sts = util$write_exact(l_fd, l_line, strlen(l_line));
		}

	close(l_fd);

	return	l_sts;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Restores the states of the queues written beside the journal.
**
**	A queue the command line named keeps what the command line gave it: an administrator who has
**	just said what a limit is to be does not expect the previous run to overrule him. A queue only
**	the file knows of is added as it was found - it holds entries, and they must have somewhere to
**	wait that can be seen.
**
**  INPUT PARAMETERS:
**	None.
**
**  OUTPUT PARAMETERS:
**	None.
**
**  RETURNS:
**	BATCH$_NORMAL.
**--
*/
static	int	s_batchd$que_load (void)
{
BATCHD$QUEUE *	l_que;
char		l_spec [BATCH$K_PATHSZ], l_line [BATCH$K_QUESZ + 64], l_name [BATCH$K_QUESZ];
int		l_fd, l_lim, l_pau, l_n = 0;
char		l_ch;

	if ( $IS_FAILURE(util$mkfspec(l_spec, sizeof(l_spec), "!AZ/!AZ",
				(const fao_prm_t []) { (fao_prm_t) g_root, (fao_prm_t) BATCHD$K_QUENAME }, 2)) )
		return	BATCH$_NORMAL;

	if ( 0 > (l_fd = open(l_spec, O_RDONLY)) )
		return	BATCH$_NORMAL;

	while ( 1 == read(l_fd, &l_ch, 1) )
		{
		if ( (l_ch != '\n') && (l_n < (int) sizeof(l_line) - 1) )
			{
			l_line[l_n++] = l_ch;
			continue;
			}

		l_line[l_n]	= '\0';
		l_n		= 0;

		l_name[0] = '\0';
		l_lim	= 0;
		l_pau	= 0;

		/*
		**  The line is taken apart here rather than by SSCANF: this subsystem includes no STDIO
		**  at all, and a routine of it that compiles only because its declaration arrived by some
		**  other header is not a thing to depend upon.
		*/
		{
		char *	l_p = l_line;
		char *	l_w = l_name;

		while ( (*l_p == ' ') || (*l_p == '\t') )
			l_p++;

		while ( (*l_p) && (*l_p != ' ') && (l_w < l_name + sizeof(l_name) - 1) )
			*l_w++ = *l_p++;

		*l_w = '\0';

		if ( !l_name[0] )
			continue;

		l_lim = (int) strtol(l_p, &l_p, 10);
		l_pau = (int) strtol(l_p, &l_p, 10);
		}

		if ( $IS_SUCCESS(s_batchd$que_by_name(l_name, &l_que)) )
			{
			/* Named on the command line as well: only the paused state is restored */
			l_que->qe_paused = l_pau;

			continue;
			}

		s_batchd$que_add(l_name, l_lim, l_pau);
		}

	close(l_fd);

	return	BATCH$_NORMAL;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Adds a queue to the table, or leaves it as it stands where the name is already there.
**
**  INPUT PARAMETERS:
**	a_name		- the name, not NULL.
**	a_joblim	- jobs of it that may execute at once.
**	a_paused	- non-zero to add it paused.
**
**  OUTPUT PARAMETERS:
**	None.
**
**  RETURNS:
**	BATCH$_NORMAL, or BATCH$_NOSLOT where the table is full.
**--
*/
static	int	s_batchd$que_add (
			const char *	a_name,
			int		a_joblim,
			int		a_paused
			)
{
BATCHD$QUEUE *	l_que;

	if ( $IS_SUCCESS(s_batchd$que_by_name(a_name, &l_que)) )
		return	BATCH$_NORMAL;

	if ( g_quecnt >= BATCH$K_MAXQUE )
		return	BATCH$_NOSLOT;

	util$mkfspec(g_queues[g_quecnt].qe_name, sizeof(g_queues[0].qe_name), "!AZ",
			(const fao_prm_t []) { (fao_prm_t) a_name }, 1);

	g_queues[g_quecnt].qe_joblim	= ((a_joblim < 1) || (a_joblim > BATCH$K_MAXJOB)) ? g_joblim : a_joblim;
	g_queues[g_quecnt].qe_paused	= a_paused;

	g_quecnt++;

	return	BATCH$_NORMAL;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Finds a queue by name.
**
**  INPUT PARAMETERS:
**	a_name	- the name, not NULL.
**
**  OUTPUT PARAMETERS:
**	a_que	- receives the queue, or NULL where the table does not hold it.
**
**  RETURNS:
**	BATCH$_NORMAL, or BATCH$_NOSUCHQUE.
**--
*/
static	int	s_batchd$que_by_name (
			const char *		a_name,
			BATCHD$QUEUE **		a_que
			)
{
	*a_que = NULL;

	for ( int i = 0; i < g_quecnt; i++ )
		{
		if ( !strcasecmp(g_queues[i].qe_name, a_name) )
			{
			*a_que = &g_queues[i];

			return	BATCH$_NORMAL;
			}
		}

	return	BATCH$_NOSUCHQUE;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Counts the jobs occupying an execution slot. A job being aborted still occupies its slot, so
**	the ABORTING state is counted along with EXECUTING.
**
**  INPUT PARAMETERS:
**	None.
**
**  OUTPUT PARAMETERS:
**	a_count	- receives the number of occupied execution slots, not NULL.
**
**  RETURNS:
**	BATCH$_NORMAL.
**--
*/
static	int	s_batchd$count_executing (
			const char *	a_queue,
			int *	a_count
			)
{
BATCHD$JOBENT *	l_ent;
int		l_n;

	l_n = 0;

	for ( l_ent = $JOBQUE_FIRST(); l_ent; l_ent = $JOBQUE_NEXT(l_ent) )
		{
		if ( (l_ent->je_job.jb_state != JOB$K_EXECUTING) && (l_ent->je_job.jb_state != JOB$K_ABORTING) )
			continue;

		/* Of one queue where a name was given, of all of them where none was */
		if ( (a_queue) && (strcasecmp(l_ent->je_job.jb_queue, a_queue)) )
			continue;

		l_n++;
		}

	*a_count = l_n;

	return	BATCH$_NORMAL;
}

/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Writes the whole job table to the journal. The write is directed to a temporary file which is
**	then renamed over the previous copy. A rename within one file system is atomic, so the journal
**	is never left in a partially written state.
**
**  INPUT PARAMETERS:
**	None.
**
**  OUTPUT PARAMETERS:
**	None.
**
**  RETURNS:
**	BATCH$_NORMAL, BATCH$_BUFOVF, BATCH$_OPENOUT, BATCH$_WRITEERR or BATCH$_RENAMEERR.
**--
*/
static	int	s_batchd$journal_save (void)
{
BATCHD$JRNHDR	l_hdr;
BATCHD$JOBENT *	l_ent;
char		l_tmp [BATCH$K_PATHSZ], l_fin [BATCH$K_PATHSZ];
unsigned char	l_rec [BATCHJRN$K_RECMAX];
int		l_fd, l_sts, l_rc;

	if ( $IS_FAILURE(util$mkfspec(l_tmp, sizeof(l_tmp), "!AZ/!AZ",
				(const fao_prm_t []) { (fao_prm_t) g_root, (fao_prm_t) BATCHD$K_JRNTMP }, 2)) )
		return	BATCH$_BUFOVF;

	if ( $IS_FAILURE(util$mkfspec(l_fin, sizeof(l_fin), "!AZ/!AZ",
				(const fao_prm_t []) { (fao_prm_t) g_root, (fao_prm_t) BATCHD$K_JRNNAME }, 2)) )
		return	BATCH$_BUFOVF;

	if ( 0 > (l_fd = open(l_tmp, O_WRONLY | O_CREAT | O_TRUNC, 0600)) )
		return	$PUTMSG_FAO(BATCH$_OPENOUT, l_tmp, l_fd, errno, strerror(errno));

	/* Fill in the header, counting the occupied slots as we go */
	l_hdr.jh_magic	= IPC$K_MAGIC;
	l_hdr.jh_version = BATCHJRN$K_VERSION;
	l_hdr.jh_entry	= g_entry;
	l_hdr.jh_count	= g_jobque.count;

	l_sts = util$write_exact(l_fd, &l_hdr, sizeof(l_hdr));

	/* The descriptor alone is written; the links that carry it through the queue are not */
	for ( l_ent = $JOBQUE_FIRST(); ($IS_SUCCESS(l_sts)) && (l_ent); l_ent = $JOBQUE_NEXT(l_ent) )
		{
		unsigned	l_reclen = 0;

		if ( $IS_FAILURE(l_sts = util$job_encode(&l_ent->je_job, l_rec, sizeof(l_rec), &l_reclen)) )
			break;

		/*
		**  The length precedes the record so that a reader which cannot make sense of one may step
		**  over it and go on to the next rather than losing the whole of the queue.
		*/
		if ( $IS_FAILURE(l_sts = util$write_exact(l_fd, &l_reclen, sizeof(l_reclen))) )
			break;

		l_sts = util$write_exact(l_fd, l_rec, l_reclen);
		}

	if ( $IS_FAILURE(l_sts) )
		{
		close(l_fd);
		unlink(l_tmp);

		return	$PUTMSG_FAO(BATCH$_WRITEERR, l_tmp, errno, strerror(errno));
		}

	/*
	**  The states of the queues go beside the journal rather than into it. The journal is a
	**  sequence of job records and says so in its header; a second kind of record within it would
	**  have to be counted somewhere, and the header is what a reader of another revision relies
	**  upon. A file of its own costs nothing and keeps the promise.
	*/
	s_batchd$que_save();

	/* Flush to the medium before renaming, otherwise the atomicity is illusory */
	fsync(l_fd);
	close(l_fd);

	if ( 0 > (l_rc = rename(l_tmp, l_fin)) )
		return	$PUTMSG_FAO(BATCH$_RENAMEERR, l_tmp, l_fin, l_rc, errno, strerror(errno));

	return	BATCH$_NORMAL;
}

/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Loads the job table from the journal when the job controller starts. Jobs that were in the
**	EXECUTING or ABORTING state when the previous instance stopped are moved to RETAINED and
**	marked as terminated abnormally: the processes they created are not children of the new
**	instance, which can therefore no longer obtain their exit status through waitpid(2).
**
**  INPUT PARAMETERS:
**	None.
**
**  OUTPUT PARAMETERS:
**	None.
**
**  RETURNS:
**	BATCH$_JRNLOADED on a successful load, BATCH$_NOJOURNAL if there is no journal, BATCH$_BUFOVF,
**	BATCH$_OPENIN, BATCH$_JRNCORRUPT or BATCH$_JRNVERSION on an unusable journal.
**--
*/
static	int	s_batchd$journal_load (void)
{
BATCHD$JRNHDR	l_hdr;
BATCHD$JOBENT *	l_ent;
BATCH$JOB	l_job;
unsigned char	l_rec [BATCHJRN$K_RECMAX];
char		l_fin [BATCH$K_PATHSZ];
int		l_fd;
unsigned int	l_i;

	if ( $IS_FAILURE(util$mkfspec(l_fin, sizeof(l_fin), "!AZ/!AZ",
				(const fao_prm_t []) { (fao_prm_t) g_root, (fao_prm_t) BATCHD$K_JRNNAME }, 2)) )
		return	BATCH$_BUFOVF;

	l_fd = open(l_fin, O_RDONLY);

	if ( (0 > l_fd) && (errno == ENOENT) )
		return	(g_verbose) ? $PUTMSG_FAO(BATCH$_NOJOURNAL, l_fin) : BATCH$_NOJOURNAL;

	if ( 0 > l_fd )
		return	$PUTMSG_FAO(BATCH$_OPENIN, l_fin, l_fd, errno, strerror(errno));

	if ( $IS_FAILURE(util$read_exact(l_fd, &l_hdr, sizeof(l_hdr))) )
		{
		close(l_fd);

		return	$PUTMSG_FAO(BATCH$_JRNCORRUPT, l_fin);
		}

	/*
	**  A record is a sequence of tagged values, so the layout of the descriptor may change without
	**  the journal becoming unreadable. What is refused here is a journal of an altogether older
	**  format - one written before the records described themselves - which cannot be read at all.
	*/
	if ( (l_hdr.jh_magic != IPC$K_MAGIC) || (l_hdr.jh_version != BATCHJRN$K_VERSION) )
		{
		close(l_fd);

		return	$PUTMSG_FAO(BATCH$_JRNVERSION, l_fin, l_hdr.jh_magic, l_hdr.jh_version);
		}

	g_entry = l_hdr.jh_entry;

	/* What an administrator set, before the entries that depend upon it are read */
	s_batchd$que_load();

	for ( l_i = 0; l_i < l_hdr.jh_count; l_i++ )
		{
		unsigned	l_reclen = 0;

		if ( $IS_FAILURE(util$read_exact(l_fd, &l_reclen, sizeof(l_reclen)))
			|| (l_reclen > sizeof(l_rec))
			|| ($IS_FAILURE(util$read_exact(l_fd, l_rec, l_reclen))) )
			{
			$PUTMSG_FAO(BATCH$_JRNTRUNC, l_fin, l_i, l_hdr.jh_count);
			break;
			}

		/*
		**  A record that will not decode is passed over rather than ending the load: one record
		**  of a hundred spoiled is the loss of one job, whereas stopping here would be the loss
		**  of the ninety-nine that follow it.
		*/
		if ( $IS_FAILURE(util$job_decode(l_rec, l_reclen, &l_job)) )
			{
			$PUTMSG_FAO(BATCH$_JRNCORRUPT, l_fin);
			continue;
			}

		if ( $IS_FAILURE(s_batchd$job_alloc(&l_ent)) )
			{
			$PUTMSG_FAO(BATCH$_NOSLOT, BATCH$K_MAXJOB, "loading the journal");
			break;
			}

		/*
		**  A job that was executing when the controller stopped is orphaned: its process is not
		**  a child of this instance and its exit status can no longer be obtained. One submitted
		**  with /RESTART goes back into the queue to be run again; the rest are kept with an exit
		**  status of -1, the true one being unobtainable.
		**
		**  The process the earlier instance created is not sought out and stopped. It is a session
		**  of its own and may well have finished; what /RESTART promises is that the job will be
		**  run, not that it will be run once.
		*/
		if ( (l_job.jb_state == JOB$K_EXECUTING) || (l_job.jb_state == JOB$K_ABORTING) )
			{
			if ( l_job.jb_restart )
				{
				l_job.jb_state	= JOB$K_PENDING;
				l_job.jb_pid	= 0;
				l_job.jb_stime	= 0;
				l_job.jb_etime	= 0;
				l_job.jb_restarts++;

				$PUTMSG_FAO(BATCH$_JOBREQUEUED, l_job.jb_entry, l_job.jb_name, l_job.jb_restarts);
				}
			else	{
				l_job.jb_state	= JOB$K_RETAINED;
				l_job.jb_exit	= -1;
				l_job.jb_signal	= 0;
				l_job.jb_etime	= (long long) time(NULL);

				$PUTMSG_FAO(BATCH$_JOBORPHAN, l_job.jb_entry);
				}
			}

		l_ent->je_job = l_job;
		}

	close(l_fd);

	/*
	**  An entry whose queue the table does not hold. It can only have come from a run configured
	**  otherwise, and left as it is it would wait for ever while looking exactly like an entry
	**  waiting its turn - which is the one thing this design set out to prevent. The queue is
	**  therefore created, and created paused: the entry is preserved, it is visible in SHOW/QUEUES
	**  with the reason it stands still, and setting it going again is one command away.
	*/
	for ( l_ent = $JOBQUE_FIRST(); l_ent; l_ent = $JOBQUE_NEXT(l_ent) )
		{
		BATCHD$QUEUE *	l_q;

		if ( !l_ent->je_job.jb_queue[0] )
			continue;

		if ( $IS_SUCCESS(s_batchd$que_by_name(l_ent->je_job.jb_queue, &l_q)) )
			continue;

		if ( $IS_SUCCESS(s_batchd$que_add(l_ent->je_job.jb_queue, g_joblim, 1)) )
			$PUTMSG_FAO(BATCH$_QUEORPHAN, l_ent->je_job.jb_queue);
		}

	return	(g_verbose) ? $PUTMSG_FAO(BATCH$_JRNLOADED, l_hdr.jh_count) : BATCH$_JRNLOADED;
}

/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Sweeps the log directory of the subsystem, removing what no longer belongs to anything.
**
**	A job log is ordinarily removed with the queue entry it belongs to. Two things escape that:
**	the log of a job submitted with /KEEP, which is deliberately left behind, and the log of a job
**	whose entry did not survive - a controller killed outright, a file system full at the wrong
**	moment. Neither is ever looked at again after a while, and nothing else would remove them.
**
**	A file is removed when it is older than the period allowed and belongs to no entry now in the
**	queue. The second test matters: a job may have been running for longer than the period, and
**	its log is being written to.
**
**	Only the log directory of the subsystem is walked. A log named by /LOG_FILE elsewhere is the
**	business of whoever named it and is never touched.
**
**  INPUT PARAMETERS:
**	None.
**
**  OUTPUT PARAMETERS:
**	a_removed - receives the number of files removed, not NULL.
**
**  RETURNS:
**	BATCH$_NORMAL, BATCH$_BUFOVF or BATCH$_NODIR.
**--
*/
static	int	s_batchd$log_sweep (
			int *	a_removed
			)
{
BATCHD$JOBENT *	l_ent;
struct dirent *	l_de;
struct stat	l_st;
DIR *		l_dir;
long long	l_now, l_age;
char		l_path [BATCH$K_PATHSZ], l_spec [BATCH$K_PATHSZ];
int		l_n, l_live;

	*a_removed	= 0;
	l_n		= 0;

	if ( !g_logkeep )
		return	BATCH$_NORMAL;

	if ( $IS_FAILURE(util$mkfspec(l_path, sizeof(l_path), "!AZ/!AZ",
				(const fao_prm_t []) { (fao_prm_t) g_root,
							(fao_prm_t) BATCHD$K_LOGDIR }, 2)) )
		return	BATCH$_BUFOVF;

	if ( !(l_dir = opendir(l_path)) )
		return	$PUTMSG_FAO(BATCH$_NODIR, l_path, errno, strerror(errno));

	l_now	= (long long) time(NULL);
	l_age	= (long long) g_logkeep * 86400LL;

	while ( (l_de = readdir(l_dir)) )
		{
		if ( (!strcmp(l_de->d_name, ".")) || (!strcmp(l_de->d_name, "..")) )
			continue;

		if ( $IS_FAILURE(util$mkfspec(l_spec, sizeof(l_spec), "!AZ/!AZ",
					(const fao_prm_t []) { (fao_prm_t) l_path,
								(fao_prm_t) l_de->d_name }, 2)) )
			continue;

		if ( (stat(l_spec, &l_st) == -1) || (!S_ISREG(l_st.st_mode)) )
			continue;

		if ( ((long long) l_st.st_mtime + l_age) > l_now )
			continue;

		/* A log an entry still owns is left alone, however old it is */
		l_live = 0;

		for ( l_ent = $JOBQUE_FIRST(); l_ent; l_ent = $JOBQUE_NEXT(l_ent) )
			{
			if ( !strcmp(l_ent->je_job.jb_logfile, l_spec) )
				{
				l_live = 1;
				break;
				}
			}

		if ( l_live )
			continue;

		if ( !unlink(l_spec) )
			l_n++;
		}

	closedir(l_dir);

	*a_removed = l_n;

	if ( l_n )
		$PUTMSG_FAO(BATCH$_LOGSWEPT, l_n, (l_n == 1) ? "" : "s", l_path, g_logkeep,
				(g_logkeep == 1) ? "" : "s");

	return	BATCH$_NORMAL;
}

/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Tells whether a specification names a file under the log directory of the subsystem.
**
**	It is asked immediately before the one open this controller performs while it still holds its
**	privileges, and it is a guard rather than a rule: the rules are applied at submission, where
**	the name of a job is required to be a name and every path to be absolute. A guard placed at
**	the act itself costs one comparison and does not depend on every one of those rules having
**	been remembered.
**
**  INPUT PARAMETERS:
**	a_path	- the specification, not NULL.
**
**  OUTPUT PARAMETERS:
**	None.
**
**  RETURNS:
**	Non-zero where the path lies under the log directory.
**--
*/
static	int	s_batchd$under_logdir (
			const char *	a_path
			)
{
char		l_dir [BATCH$K_PATHSZ];
size_t		l_len;

	if ( a_path[0] != '/' )
		return	0;

	if ( $IS_FAILURE(util$mkfspec(l_dir, sizeof(l_dir), "!AZ/!AZ",
				(const fao_prm_t []) { (fao_prm_t) g_root, (fao_prm_t) BATCHD$K_LOGDIR }, 2)) )
		return	0;

	l_len = strlen(l_dir);

	if ( strncmp(a_path, l_dir, l_len) )
		return	0;

	/* The directory itself is not a file of it, and a longer name is not a component of it */
	if ( a_path[l_len] != '/' )
		return	0;

	/* A parent reference would walk out again, the prefix notwithstanding */
	for ( const char *l_p = a_path; *l_p; l_p++ )
		{
		if ( (l_p[0] == '.') && (l_p[1] == '.') && ((l_p[2] == '/') || (!l_p[2]))
			&& ((l_p == a_path) || (l_p[-1] == '/')) )
			return	0;
		}

	return	1;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Creates the process that executes a job. In the created process the following are performed in
**	order: creation of a new session, restoration of the default signal disposition, assumption of
**	the submitter's identity, change of the working directory, opening of the job log with
**	replacement of the standard input, output and error descriptors, establishment of the
**	environment variables BATCH_ENTRY, BATCH_NAME, BATCH_QUEUE and P1 through P8, and finally
**	replacement of the image.
**
**	Creation of a new session is essential: it makes the job the leader of its own process group,
**	which afterwards permits the whole tree of processes it created to be terminated by a single
**	call to kill(-pid, SIGTERM).
**
**  INPUT PARAMETERS:
**	a_ent	- address of the queue entry; the job is to be in the JOB$K_PENDING state.
**
**  OUTPUT PARAMETERS:
**	The fields jb_pid, jb_stime and jb_state of the descriptor are updated. If the process cannot be
**	created, the job is moved to the JOB$K_RETAINED state with an exit status of -1.
**
**  RETURNS:
**	BATCH$_JOBSTART if the process was created, otherwise BATCH$_FORKERR.
**--
*/
static	int	s_batchd$job_start (
			BATCHD$JOBENT *	a_ent
			)
{
BATCH$JOB *	l_job;
cpu_set_t	l_cpus;
char *		l_argv [BATCH$K_NPARM + 3];
char		l_env [64];
pid_t		l_pid;
struct stat	l_st;
int		l_fd, l_log = -1, l_i, l_argc, l_ncpu, l_core = -1;

	/*
	**  The processor this job is to have, taken from the pool in turn. It is chosen here, in the
	**  parent, rather than after the fork: the rotation is a property of the queue and must be
	**  advanced once per job, whereas a child could only advance a copy of it.
	*/
	l_job	= &a_ent->je_job;

	/*
	**  The script is identified again before anything is created. It was pinned at submission by
	**  device, inode, size and time of change, and the path may since have been made to name
	**  another file altogether - which is the whole of what a path guarantees.
	**
	**  The refusal is issued here, in the controller, rather than left to the child: a child can
	**  only exit, and an exit says exit=126 and no more. The entry is retained with the reason
	**  named, which is what whoever submitted the job needs to read.
	*/
	if ( (0 > stat(l_job->jb_script, &l_st)) || (!S_ISREG(l_st.st_mode))
		|| ((long long) l_st.st_dev != l_job->jb_dev)
		|| ((long long) l_st.st_ino != l_job->jb_ino)
		|| ((long long) l_st.st_size != l_job->jb_size)
		|| (((long long) l_st.st_mtim.tv_sec * 1000000000LL
			+ (long long) l_st.st_mtim.tv_nsec) != l_job->jb_mtime) )
		{
		l_job->jb_state	= JOB$K_RETAINED;
		l_job->jb_exit	= -1;
		l_job->jb_etime	= (long long) time(NULL);

		return	$PUTMSG_FAO(BATCH$_SCRIPTCHG, l_job->jb_entry, l_job->jb_script);
		}

	if ( (!l_job->jb_cpus[0]) && (g_lcorecnt) )
		{
		l_core		= g_lcore[g_lcorenext];
		g_lcorenext	= (g_lcorenext + 1) % g_lcorecnt;
		}

	l_pid	= fork();

	if ( 0 > l_pid )
		{
		l_job->jb_state	= JOB$K_RETAINED;
		l_job->jb_exit	= -1;
		l_job->jb_etime	= (long long) time(NULL);

		return	$PUTMSG_FAO(BATCH$_FORKERR, l_job->jb_entry, (int) l_pid, errno, strerror(errno));
		}

	if ( !l_pid )
		{
		/*
		**  The created process. Any failure here exits with BATCHD$K_EXECFAIL: the reaper will
		**  collect that status and the job entry will be kept with an indication of error.
		**  Issuing a message from here is inadmissible - it would land in the job log.
		*/
		setsid();

		/* Restore the default signal disposition inherited from the daemon */
		s_batchd$sigaction(SIGCHLD, SIG_DFL);
		s_batchd$sigaction(SIGTERM, SIG_DFL);
		s_batchd$sigaction(SIGINT,  SIG_DFL);
		s_batchd$sigaction(SIGHUP,  SIG_IGN);

		/*
		**  The processor affinity is established before the identity is assumed: a process may
		**  always set its own, whereas whether it may do so for another is a separate question
		**  the job has no need to raise.
		*/
		if ( l_job->jb_cpus[0] )
			{
			if ( $IS_FAILURE(util$parse_cpulist(l_job->jb_cpus, &l_cpus, &l_ncpu)) )
				_exit(BATCHD$K_EXECFAIL);

			if ( 0 > sched_setaffinity(0, sizeof(l_cpus), &l_cpus) )
				_exit(BATCHD$K_EXECFAIL);
			}
		else if ( l_core >= 0 )
			{
			CPU_ZERO(&l_cpus);
			CPU_SET((unsigned) l_core, &l_cpus);

			if ( 0 > sched_setaffinity(0, sizeof(l_cpus), &l_cpus) )
				_exit(BATCHD$K_EXECFAIL);
			}

		/*
		**  The log is opened before the identity is assumed and is made over to the submitter
		**  afterwards. Opened after, it could not be created where the submitter may not write -
		**  the log directory of the subsystem belongs to the privileged user - and a job of an
		**  ordinary user would fail before its first line, with nowhere to say why.
		*/
		/*
		**  A path this controller built is opened here, while the privilege to do so is in hand,
		**  and is made over to the user the job runs as: it lies in a directory of the subsystem
		**  or in the one the job was submitted from, and its name was composed here.
		**
		**  Only a path under the log directory of the subsystem is opened here - one this
		**  controller both named and placed. A path beside the submission is built here too, but
		**  from a directory the client supplied, and the name of the job is the client's as well:
		**  opening it with privilege would create or truncate, say, /etc/cron.d/anything_1.log on
		**  a client's word. It is opened after the drop, where the submitter's own rights decide -
		**  which is no loss, that path having been chosen only because he could write there.
		**
		**  A path the client named outright is not opened here at any price. Doing so would have the
		**  privileged user create, and truncate, whatever file a client cared to name - a
		**  configuration, a key, a crontab - and the job would then carry on under its own uid,
		**  the damage already done. Such a path waits until the identity has been assumed.
		*/
		if ( (!l_job->jb_logclient) && (!l_job->jb_logcwd) && (s_batchd$under_logdir(l_job->jb_logfile)) )
			{
			if ( 0 > (l_log = open(l_job->jb_logfile, O_WRONLY | O_CREAT | O_TRUNC, 0640)) )
				_exit(BATCHD$K_EXECFAIL);

			/* Best effort: the job writes through the descriptor whoever owns the file */
			if ( !geteuid() )
				{
				if ( fchown(l_log, (uid_t) l_job->jb_uid, (gid_t) l_job->jb_gid) )
					{ }
				}
			}

		/* Assuming another identity is possible, and needed, only if the daemon runs as root */
		if ( !geteuid() )
			{
			struct passwd *	l_pw = getpwuid((uid_t) l_job->jb_uid);

			/*
			**  The supplementary groups of the controller are not the job's to keep: SETUID
			**  leaves them as they were, so a job of an ordinary user would go on holding
			**  whatever memberships the privileged daemon had. They are replaced by those of the
			**  user the job runs as, or emptied where his name cannot be had.
			*/
			if ( l_pw )
				{
				if ( initgroups(l_pw->pw_name, (gid_t) l_job->jb_gid) == -1 )
					_exit(BATCHD$K_EXECFAIL);
				}
			else if ( setgroups(0, NULL) == -1 )
				_exit(BATCHD$K_EXECFAIL);

			if ( setgid((gid_t) l_job->jb_gid) == -1 )
				_exit(BATCHD$K_EXECFAIL);

			if ( setuid((uid_t) l_job->jb_uid) == -1 )
				_exit(BATCHD$K_EXECFAIL);
			}

		/* The directory may have vanished since submission; fall back to the root */
		/*
		**  The log the client named, opened now that the job's own identity is in force, so the
		**  file system decides by that user's rights and not by the controller's. O_NOFOLLOW
		**  refuses a symbolic link: a path the user may write to is one thing, a link from it to
		**  a file he may not is another.
		*/
		if ( l_log < 0 )
			{
			if ( 0 > (l_log = open(l_job->jb_logfile, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0640)) )
				_exit(BATCHD$K_EXECFAIL);
			}

		/*
		**  The script is looked at again, now under the identity that will run it. It was looked
		**  at once already, at submission, but by the controller - so that check said whether the
		**  privileged user could read the file, which is not the question, and a file may in any
		**  case have been replaced in the meantime.
		*/
		if ( 0 > (l_fd = open(l_job->jb_script, O_RDONLY)) )
			_exit(BATCHD$K_EXECFAIL);

		/*
		**  And it must be the file that was submitted. Between the submission and now the path
		**  could have been made to name another file altogether, which is the whole of what a
		**  path guarantees; the device, the inode and the time of the last change say whether it
		**  did. A job whose script has changed under it is not the job that was submitted.
		*/
		if ( 0 > fstat(l_fd, &l_st) )
			_exit(BATCHD$K_EXECFAIL);

		if ( ((long long) l_st.st_dev != l_job->jb_dev) || ((long long) l_st.st_ino != l_job->jb_ino)
			|| ((long long) l_st.st_size != l_job->jb_size)
			|| (((long long) l_st.st_mtim.tv_sec * 1000000000LL
				+ (long long) l_st.st_mtim.tv_nsec) != l_job->jb_mtime) )
			_exit(BATCHD$K_EXECFAIL);

		close(l_fd);

		if ( chdir(l_job->jb_cwd) == -1 )
			{
			if ( chdir("/") == -1 )
				_exit(BATCHD$K_EXECFAIL);
			}

		/* A batch job runs without input: an empty source is substituted */
		l_fd = open("/dev/null", O_RDONLY);

		if ( l_fd != -1 )
			{
			dup2(l_fd, STDIN_FILENO);

			if ( l_fd != STDIN_FILENO )
				close(l_fd);
			}

		/* The output and diagnostics of the job go to its log file */
		dup2(l_log, STDOUT_FILENO);
		dup2(l_log, STDERR_FILENO);

		if ( (l_log != STDOUT_FILENO) && (l_log != STDERR_FILENO) )
			close(l_log);

		util$mkfspec(l_env, sizeof(l_env), "!UL", (const fao_prm_t []) { (fao_prm_t) l_job->jb_entry }, 1);
		setenv("BATCH_ENTRY", l_env, 1);
		setenv("BATCH_NAME", l_job->jb_name, 1);
		setenv("BATCH_QUEUE", l_job->jb_queue, 1);

		/*
		**  The job is told whether it is being run again, in the manner of the $RESTART symbol
		**  of OpenVMS, and how many times it has been.
		*/
		setenv("BATCH_RESTART", (l_job->jb_restarts) ? "TRUE" : "FALSE", 1);

		util$mkfspec(l_env, sizeof(l_env), "!UL",
				(const fao_prm_t []) { (fao_prm_t) l_job->jb_restarts }, 1);
		setenv("BATCH_RESTARTS", l_env, 1);

		/*
		**  Parameters reach the job in two ways: as the environment variables P1 through P8 and
		**  as the positional arguments of the command file. Passing them as separate elements of
		**  the argument vector precludes parsing of a command line by the interpreter and with it
		**  any possibility of substitution.
		*/
		for ( l_i = 0; l_i < BATCH$K_NPARM; l_i++ )
			{
			util$mkfspec(l_env, sizeof(l_env), "P!SL", (const fao_prm_t []) { (fao_prm_t) (l_i + 1) }, 1);
			setenv(l_env, l_job->jb_parm[l_i], 1);
			}

		l_argc			= 0;
		l_argv[l_argc++]	= s_shname;
		l_argv[l_argc++]	= l_job->jb_script;

		for ( l_i = 0; l_i < BATCH$K_NPARM; l_i++ )
			{
			if ( l_job->jb_parm[l_i][0] )
				l_argv[l_argc++] = l_job->jb_parm[l_i];
			}

		l_argv[l_argc] = NULL;

		execv(BATCH$K_SHELL, l_argv);
		_exit(BATCHD$K_EXECFAIL);
		}

	l_job->jb_pid	= (int) l_pid;
	l_job->jb_stime	= (long long) time(NULL);
	l_job->jb_state	= JOB$K_EXECUTING;

	return	$PUTMSG_FAO(BATCH$_JOBSTART, l_job->jb_entry, l_job->jb_name, l_job->jb_pid, l_job->jb_logfile);
}

/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Scheduler of the queue. It performs two actions. First, it moves jobs from HOLDING to PENDING
**	when their /AFTER time arrives. Second, it starts PENDING jobs for as long as the number of
**	occupied execution slots remains below the established limit.
**
**	The candidate is chosen by the highest priority and, among jobs of equal priority, by the
**	lowest entry number. Jobs of equal priority are thereby served in the order of submission.
**
**  INPUT PARAMETERS:
**	None.
**
**  OUTPUT PARAMETERS:
**	a_changed - receives the number of changes made to the queue on this pass, not NULL. A nonzero
**		    value calls for the journal to be written.
**
**  RETURNS:
**	BATCH$_NORMAL.
**--
*/
static	int	s_batchd$schedule_que (BATCHD$QUEUE *a_que, int a_free, long long a_now, int *a_changed);

static	int	s_batchd$schedule (
			int *	a_changed
			)
{
BATCHD$JOBENT *	l_ent, *l_best;
long long	l_now;
int		l_free, l_busy;

	l_now = (long long) time(NULL);

	if ( a_changed )
		*a_changed = 0;

	/* Release the jobs whose appointed start time has arrived */
	for ( l_ent = $JOBQUE_FIRST(); l_ent; l_ent = $JOBQUE_NEXT(l_ent) )
		{
		if ( l_ent->je_job.jb_state != JOB$K_HOLDING )
			continue;

		if ( (!l_ent->je_job.jb_after) || (l_ent->je_job.jb_after > l_now) )
			continue;

		l_ent->je_job.jb_state = JOB$K_PENDING;

		if ( a_changed )
			(*a_changed)++;
		}

	/*
	**  Each queue is filled in turn and to its own limit. A queue that is paused is passed over
	**  entirely: pausing is what one does to a queue whose jobs must wait without their entries
	**  being disturbed.
	*/
	for ( int q = 0; q < g_quecnt; q++ )
		{
		if ( g_queues[q].qe_paused )
			continue;

		s_batchd$count_executing(g_queues[q].qe_name, &l_busy);

		l_free = g_queues[q].qe_joblim - l_busy;

		s_batchd$schedule_que(&g_queues[q], l_free, l_now, a_changed);
		}

	return	BATCH$_NORMAL;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Starts what may be started in one queue, up to the places it has free.
**
**  INPUT PARAMETERS:
**	a_que	- the queue, not NULL.
**	a_free	- places free in it.
**	a_now	- the present time, so that every job of one pass is judged against one instant.
**
**  OUTPUT PARAMETERS:
**	None.
**
**  RETURNS:
**	BATCH$_NORMAL.
**--
*/
static	int	s_batchd$schedule_que (
			BATCHD$QUEUE *	a_que,
			int		a_free,
			long long	a_now,
			int *		a_changed
			)
{
BATCHD$JOBENT *	l_ent, *l_best;
int		l_free = a_free;
long long	l_now = a_now;

	while ( l_free > 0 )
		{
		l_best = NULL;

		for ( l_ent = $JOBQUE_FIRST(); l_ent; l_ent = $JOBQUE_NEXT(l_ent) )
			{
			if ( strcasecmp(l_ent->je_job.jb_queue, a_que->qe_name) )
				continue;

			if ( l_ent->je_job.jb_state != JOB$K_PENDING )
				continue;

			/* An /AFTER time may have been set without /HOLD, so it is checked here too */
			if ( (l_ent->je_job.jb_after) && (l_ent->je_job.jb_after > l_now) )
				continue;

			if ( !l_best )
				{
				l_best = l_ent;
				continue;
				}

			if ( l_ent->je_job.jb_prio > l_best->je_job.jb_prio )
				l_best = l_ent;
			else if ( (l_ent->je_job.jb_prio == l_best->je_job.jb_prio)
					&& (l_ent->je_job.jb_entry < l_best->je_job.jb_entry) )
				l_best = l_ent;
			}

		if ( !l_best )
			break;

		s_batchd$job_start(l_best);

		if ( a_changed )
			(*a_changed)++;

		l_free--;
		}

	return	BATCH$_NORMAL;
}

/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Renders what an exit status above 128 most likely means.
**
**	A shell that ends because the command it was running was killed exits with 128 plus the number
**	of the signal. The number itself says nothing to a reader - 135 is not a thing anyone knows by
**	sight - and the job did not die of a signal itself, so the signal field of the descriptor is
**	empty and no other part of the display will say it either.
**
**	It is a surmise and is worded as one: a program is free to exit with 135 of its own accord,
**	and nothing here can tell the two apart.
**
**  INPUT PARAMETERS:
**	a_exit	- the exit status.
**	a_bufsz	- size of the buffer.
**
**  OUTPUT PARAMETERS:
**	a_buf	- receives the remark, empty where there is none to make.
**
**  RETURNS:
**	BATCH$_NORMAL.
**--
*/
static	int	s_batchd$exithint (
			int		a_exit,
			char *		a_buf,
			size_t		a_bufsz
			)
{
	a_buf[0] = '\0';

	if ( (a_exit > 128) && (a_exit < 128 + NSIG) )
		util$mkfspec(a_buf, a_bufsz, " (128 + !SL, likely !AZ)",
				(const fao_prm_t []) { (fao_prm_t) (a_exit - 128),
							(fao_prm_t) strsignal(a_exit - 128) }, 2);

	return	BATCH$_NORMAL;
}

/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Collects the status of every terminated child process and records the outcome in the job table.
**
**	A job that completed successfully is removed from the queue unless /RETAIN=ALWAYS was given;
**	its log is deleted unless /KEEP was given. A job that returned a nonzero status or was
**	terminated by a signal is moved to RETAINED and remains visible to the SHOW command without the
**	/ALL qualifier. This is what produces the required behaviour: an ordinary SHOW displays the
**	jobs that are executing or waiting and the jobs that failed; an entry kept after a success is
**	displayed only with /ALL.
**
**  INPUT PARAMETERS:
**	None.
**
**  OUTPUT PARAMETERS:
**	a_changed - receives the number of terminations processed, not NULL.
**
**  RETURNS:
**	BATCH$_NORMAL.
**--
*/
static	int	s_batchd$reap (
			int *	a_changed
			)
{
BATCHD$JOBENT *	l_ent;
BATCH$JOB *	l_job;
pid_t		l_pid;
int		l_status, l_n;

	l_n = 0;

	while ( (l_pid = waitpid(-1, &l_status, WNOHANG)) > 0 )
		{
		if ( $IS_FAILURE(s_batchd$job_by_pid(l_pid, &l_ent)) )
			{
			$PUTMSG_FAO(BATCH$_UNKPROC, (int) l_pid);
			continue;
			}

		l_job			= &l_ent->je_job;
		l_job->jb_etime		= (long long) time(NULL);
		l_job->jb_exit		= WIFEXITED(l_status)   ? WEXITSTATUS(l_status) : -1;
		l_job->jb_signal	= WIFSIGNALED(l_status) ? WTERMSIG(l_status)   : 0;
		l_n++;

		/* Successful completion: the entry leaves the queue, as it does in VMS */
		if ( (!l_job->jb_exit) && (!l_job->jb_signal) && (l_job->jb_retain != RETAIN$K_ALWAYS) )
			{
			$PUTMSG_FAO(BATCH$_JOBDONE, l_job->jb_entry, l_job->jb_name, l_job->jb_etime - l_job->jb_stime);

			s_batchd$job_free(l_ent);
			continue;
			}

		/* /RETAIN=NEVER discards the entry even of a job that failed */
		if ( l_job->jb_retain == RETAIN$K_NEVER )
			{
			s_batchd$job_free(l_ent);
			continue;
			}

		l_job->jb_state = JOB$K_RETAINED;

		if ( l_job->jb_signal )
			$PUTMSG_FAO(BATCH$_JOBABORT, l_job->jb_entry, l_job->jb_name, l_job->jb_signal, l_job->jb_logfile);
		else
			{
			char	l_hint [64];

			s_batchd$exithint(l_job->jb_exit, l_hint, sizeof(l_hint));

			$PUTMSG_FAO(BATCH$_JOBFAIL, l_job->jb_entry, l_job->jb_name, l_job->jb_exit, l_hint,
					l_job->jb_logfile);
			}
		}

	*a_changed = l_n;

	return	BATCH$_NORMAL;
}

/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Services an IPC$K_SUBMIT request. The job descriptor sent by the client is accepted only in
**	part: the fields jb_uid, jb_gid, jb_entry, jb_pid and the timestamps are filled in by the job
**	controller itself. An untrusted client is thereby denied any means of forging the owner of a
**	job or its queue entry number.
**
**  INPUT PARAMETERS:
**	a_job	- job descriptor sent by the client, not NULL.
**	a_uid	- user identifier of the sender, obtained from SO_PEERCRED.
**	a_gid	- group identifier of the sender, obtained from SO_PEERCRED.
**
**  OUTPUT PARAMETERS:
**	a_entry	- receives the queue entry number allocated, not NULL. Zero on rejection.
**
**  RETURNS:
**	BATCH$_JOBQUED when the job has been queued, otherwise BATCH$_NOSCRIPT, BATCH$_NOSLOT or
**	BATCH$_LOGSPEC.
**--
*/
static	int	s_batchd$do_submit (
			BATCH$JOB *	a_job,
			int		a_uid,
			int		a_gid,
			unsigned int *	a_entry
			)
{
BATCHD$JOBENT *	l_ent;
BATCHD$QUEUE *	l_que;
BATCH$JOB *	l_job;
struct stat	l_st;
cpu_set_t	l_cpus;
const char *	l_state;
int		l_rc, l_ncpu;

	*a_entry = 0;

	/*
	**  A path that will be used after the privileges have been dropped, or while they are still
	**  held for the spool log, must be absolute. A relative one would be resolved against whatever
	**  the controller happens to have for a working directory, which is not a thing the client is
	**  entitled to rely upon - nor the controller to be led by.
	*/
	if ( a_job->jb_script[0] != '/' )
		return	$PUTMSG_FAO(BATCH$_NOSCRIPT, a_job->jb_script, "not an absolute path", "submit", -1,
					EINVAL, strerror(EINVAL));

	if ( (a_job->jb_cwd[0]) && (a_job->jb_cwd[0] != '/') )
		return	$PUTMSG_FAO(BATCH$_NOSCRIPT, a_job->jb_cwd, "working directory is not absolute",
					"submit", -1, EINVAL, strerror(EINVAL));

	if ( (a_job->jb_logfile[0]) && (a_job->jb_logfile[0] != '/') )
		return	$PUTMSG_FAO(BATCH$_NOSCRIPT, a_job->jb_logfile, "log specification is not absolute",
					"submit", -1, EINVAL, strerror(EINVAL));

	/* The script is checked before a slot is taken, so as not to breed useless entries */
	if ( 0 > (l_rc = stat(a_job->jb_script, &l_st)) )
		return	$PUTMSG_FAO(BATCH$_NOSCRIPT, a_job->jb_script, "not accessible", "stat", l_rc, errno,
					strerror(errno));

	if ( (a_job->jb_cpus[0]) && ($IS_FAILURE(util$parse_cpulist(a_job->jb_cpus, &l_cpus, &l_ncpu))) )
		return	$PUTMSG_FAO(BATCH$_IVCPU, a_job->jb_cpus);

	if ( !S_ISREG(l_st.st_mode) )
		return	$PUTMSG_FAO(BATCH$_NOSCRIPT, a_job->jb_script, "not a regular file", "stat", l_rc, 0, "");

	if ( $IS_FAILURE(s_batchd$job_alloc(&l_ent)) )
		return	$PUTMSG_FAO(BATCH$_NOSLOT, BATCH$K_MAXJOB, "queueing a job");

	l_job	= &l_ent->je_job;
	*l_job	= *a_job;

	/*
	**  The name of the job becomes part of the specification of its log, and that log is the one
	**  file this controller opens while it still holds its privileges. A name carrying a path
	**  separator, or a parent reference, would lead that open out of the log directory - so the
	**  name is required to be a name and not a path.
	*/
	if ( (!l_job->jb_name[0]) || (strchr(l_job->jb_name, '/')) || (strstr(l_job->jb_name, "..")) )
		{
		s_batchd$job_free(l_ent);

		return	$PUTMSG_FAO(BATCH$_LOGSPEC);
		}

	/* The fields the client may not be trusted with are set here and only here */
	l_job->jb_entry		= g_entry++;
	/*
	**  Whether the log was named by the client is settled before anything is built, and settles in
	**  turn when the file may be opened and whether it may be removed: a path of the client's is
	**  opened after the identity of the job has been assumed and is never unlinked, so that the
	**  privileged user neither creates, truncates nor takes away a file a client merely named.
	*/
	/*
	**  The queue must be one the controller was told of. A job placed in a queue nobody minds would
	**  wait for ever and would look exactly like a job waiting its turn.
	*/
	/* A job that names no queue goes to the default one, which is the first of the table */
	if ( !l_job->jb_queue[0] )
		util$mkfspec(l_job->jb_queue, sizeof(l_job->jb_queue), "!AZ",
				(const fao_prm_t []) { (fao_prm_t) g_queue }, 1);

	if ( $IS_FAILURE(s_batchd$que_by_name(l_job->jb_queue, &l_que)) )
		{
		char	l_qn [BATCH$K_QUESZ];

		util$mkfspec(l_qn, sizeof(l_qn), "!AZ", (const fao_prm_t []) { (fao_prm_t) l_job->jb_queue }, 1);

		s_batchd$job_free(l_ent);

		return	$PUTMSG_FAO(BATCH$_NOSUCHQUE, l_qn);
		}

	l_job->jb_logclient	= (l_job->jb_logfile[0]) ? 1 : 0;

	/*
	**  The script is pinned by what it was at submission. A path says nothing about the file it
	**  names for longer than it takes to say it, so the file is identified again when the job comes
	**  to be run and must be the same one - not merely a file of the same name.
	*/
	l_job->jb_dev		= (long long) l_st.st_dev;
	l_job->jb_ino		= (long long) l_st.st_ino;
	/*
	**  To the nanosecond, and with the size beside it: a second is long enough to replace a file
	**  in, and a stamp counted in seconds would call the replacement the same file.
	*/
	l_job->jb_mtime		= (long long) l_st.st_mtim.tv_sec * 1000000000LL + (long long) l_st.st_mtim.tv_nsec;
	l_job->jb_size		= (long long) l_st.st_size;

	l_job->jb_uid		= a_uid;
	l_job->jb_gid		= a_gid;

	/*
	**  A job to run as somebody else. The identity is settled here rather than by the utility, and
	**  the request is granted only to the privileged user: the utility runs as whoever invoked it
	**  and could claim anything, whereas the identity of the sender is what the kernel reports
	**  through SO_PEERCRED and cannot be claimed at all.
	**
	**  The job then starts in the home directory of that user rather than in the directory it was
	**  submitted from, which he may well not be admitted to; and the log goes there with it, for
	**  the same reason. An explicit /LOG_FILE still names what it names.
	*/
	if ( l_job->jb_user[0] )
		{
		struct passwd *	l_pw;
		struct stat	l_home;

		/*
		**  The entry is released before the refusal is returned. It was allocated above, and a
		**  request refused must leave nothing behind: an entry left in the queue would be run,
		**  which is precisely what the refusal was for.
		*/
		if ( a_uid )
			{
			char	l_who [BATCH$K_USERSZ];

			util$mkfspec(l_who, sizeof(l_who), "!AZ", (const fao_prm_t []) { (fao_prm_t) l_job->jb_user }, 1);

			s_batchd$job_free(l_ent);

			return	$PUTMSG_FAO(BATCH$_NOPRIVUSER, a_uid, l_who);
			}

		if ( !(l_pw = getpwnam(l_job->jb_user)) )
			{
			char	l_who [BATCH$K_USERSZ];

			util$mkfspec(l_who, sizeof(l_who), "!AZ", (const fao_prm_t []) { (fao_prm_t) l_job->jb_user }, 1);

			s_batchd$job_free(l_ent);

			return	$PUTMSG_FAO(BATCH$_NOSUCHUSER, l_who);
			}

		l_job->jb_uid	= (int) l_pw->pw_uid;
		l_job->jb_gid	= (int) l_pw->pw_gid;

		/*
		**  The home directory, where there is one to be had: an account kept for a service rather
		**  than for a person often names a directory that does not exist, and starting a job in a
		**  directory that is not there fails it before its first line. The root directory serves
		**  instead - every user may enter it, and a job told to run as somebody else is ordinarily
		**  told where to work by its own script.
		*/
		if ( (l_pw->pw_dir) && (l_pw->pw_dir[0] == '/') && (!stat(l_pw->pw_dir, &l_home))
			&& (S_ISDIR(l_home.st_mode)) )
			util$mkfspec(l_job->jb_cwd, sizeof(l_job->jb_cwd), "!AZ",
					(const fao_prm_t []) { (fao_prm_t) l_pw->pw_dir }, 1);
		else	util$mkfspec(l_job->jb_cwd, sizeof(l_job->jb_cwd), "!AZ",
					(const fao_prm_t []) { (fao_prm_t) "/" }, 1);

		/*
		**  The log goes to the log directory of the subsystem. Whether the log may be written
		**  beside the submission was decided by the utility on behalf of the submitter, and the
		**  job is no longer his: the answer for one user says nothing about another.
		*/
		l_job->jb_logcwd = 0;

		/*
		**  Who asked, and for whom. A job that runs as somebody other than the one who submitted
		**  it is the one operation of this subsystem that grants an authority rather than uses
		**  one, so the log holds a record of every such grant - and of every refusal above.
		*/
		$PUTMSG_FAO(BATCH$_RUNASUSER, l_job->jb_entry, a_uid, l_job->jb_user, l_job->jb_uid, l_job->jb_gid);
		}
	l_job->jb_pid		= 0;
	l_job->jb_exit		= 0;
	l_job->jb_signal	= 0;
	l_job->jb_qtime		= (long long) time(NULL);
	l_job->jb_stime		= 0;
	l_job->jb_etime		= 0;

	if ( (l_job->jb_prio < 1) || (l_job->jb_prio > BATCH$K_MAXPRIO) )
		l_job->jb_prio = BATCH$K_DEFPRIO;

	if ( !l_job->jb_queue[0] )
		util$mkfspec(l_job->jb_queue, sizeof(l_job->jb_queue), "!AZ", (const fao_prm_t []) { (fao_prm_t) g_queue }, 1);

	/*
	**  The job log specification is built in the descriptor itself using other fields of that same
	**  descriptor, which is admissible thanks to the intermediate buffer inside UTIL$MKFSPEC.
	*/
	if ( !l_job->jb_logfile[0] )
		{
		/*
		**  The log belongs in the directory the job was submitted from, as it does in OpenVMS,
		**  and in the log directory of the subsystem when it does not: whether the submitter may
		**  write where the job starts is decided by the utility, which runs as the submitter and
		**  can simply ask, whereas the controller ordinarily runs as the privileged user and
		**  would be told yes wherever it looked.
		*/
		if ( $IS_FAILURE((l_job->jb_logcwd)
				? util$mkfspec(l_job->jb_logfile, sizeof(l_job->jb_logfile), "!AZ/!AZ_!UL.log",
					(const fao_prm_t []) { (fao_prm_t) l_job->jb_cwd,
								(fao_prm_t) l_job->jb_name,
								(fao_prm_t) l_job->jb_entry }, 3)
				: util$mkfspec(l_job->jb_logfile, sizeof(l_job->jb_logfile),
					"!AZ/!AZ/!AZ_!UL.log",
					(const fao_prm_t []) { (fao_prm_t) g_root, (fao_prm_t) BATCHD$K_LOGDIR,
								(fao_prm_t) l_job->jb_name,
								(fao_prm_t) l_job->jb_entry }, 4)) )
			{
			s_batchd$job_free(l_ent);

			return	$PUTMSG_FAO(BATCH$_LOGSPEC);
			}
		}

	/*
	**  The state on entry. The client sets HOLDING for the /HOLD qualifier; any other state sent
	**  is reduced to PENDING. An /AFTER time in the future yields HOLDING regardless of the state
	**  that was sent.
	*/
	if ( l_job->jb_state != JOB$K_HOLDING )
		l_job->jb_state = JOB$K_PENDING;

	if ( (l_job->jb_after) && (l_job->jb_after > (long long) time(NULL)) )
		l_job->jb_state = JOB$K_HOLDING;

	*a_entry = l_job->jb_entry;
	util$state_name(l_job->jb_state, &l_state);

	return	$PUTMSG_FAO(BATCH$_JOBQUED, l_job->jb_entry, l_job->jb_name, l_job->jb_queue, l_state);
}

/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Services an IPC$K_DELETE request: aborts a job or deletes a queue entry. An executing job is
**	sent SIGTERM to its whole process group and moves to the ABORTING state; the final disposition
**	takes place in the reaper when the termination status is collected.
**
**  INPUT PARAMETERS:
**	a_entry	- queue entry number.
**	a_uid	- user identifier of the sender, obtained from SO_PEERCRED. A value of zero denotes the
**		  privileged user and waives the ownership check.
**
**  OUTPUT PARAMETERS:
**	None.
**
**  RETURNS:
**	BATCH$_ENTDELETED, BATCH$_ABORTREQ, BATCH$_NOSUCHENT or BATCH$_NOPRIV.
**--
*/
static	int	s_batchd$do_delete (
			unsigned int	a_entry,
			int		a_uid
			)
{
BATCHD$JOBENT *	l_ent;
BATCH$JOB *	l_job;

	if ( $IS_FAILURE(s_batchd$job_by_entry(a_entry, &l_ent)) )
		return	$PUTMSG_FAO(BATCH$_NOSUCHENT, a_entry);

	l_job = &l_ent->je_job;

	if ( (a_uid) && (l_job->jb_uid != a_uid) )
		return	$PUTMSG_FAO(BATCH$_NOPRIV, a_entry, l_job->jb_uid);

	/*
	**  The signal is sent to the process group rather than to a single process: the job is the
	**  leader of its group, and the whole tree it created is to be terminated.
	*/
	if ( l_job->jb_state == JOB$K_EXECUTING )
		{
		l_job->jb_state = JOB$K_ABORTING;
		kill(-((pid_t) l_job->jb_pid), SIGTERM);

		return	$PUTMSG_FAO(BATCH$_ABORTREQ, a_entry, l_job->jb_pid);
		}

	s_batchd$job_free(l_ent);

	return	$PUTMSG_FAO(BATCH$_ENTDELETED, a_entry);
}

/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Services an IPC$K_SETENTRY request: alters the attributes of a queue entry that the request
**	names in its mask, and no other.
**
**	What may be altered depends on the state of the job. Everything the request can carry may be
**	altered while the job waits, in the Pending or the Holding state. Once it executes, only what
**	concerns its disposition afterwards may - /RETAIN, /KEEP and /RESTART - since the rest has been
**	acted upon and altering it would describe a start that did not happen. A retained entry is not
**	altered at all: the job is over and nothing about it is still to be decided. A request that
**	carries an attribute the state forbids is refused whole, so that an entry is never half altered.
**
**	The request is verified in full before anything is touched, for the same reason.
**
**	Holding and the start time work together as they do at submission. /HOLD withholds a pending
**	job; /NOHOLD lets a held one go and clears its time, as RELEASE does; /AFTER in the future
**	withholds a pending job until then, and a job held by a time alone is let go when /NOAFTER
**	clears it, whereas one held by /HOLD stays held - a time-held job is told from an explicitly
**	held one by the time it carries, an explicitly held one carrying none. /HOLD /NOAFTER together
**	therefore mean held without a time, and the job stays until RELEASE.
**
**  INPUT PARAMETERS:
**	a_entry	- queue entry number.
**	a_uid	- user identifier of the sender, obtained from SO_PEERCRED. A value of zero denotes the
**		  privileged user and waives the ownership check.
**	a_req	- the request, not NULL.
**
**  OUTPUT PARAMETERS:
**	None.
**
**  RETURNS:
**	BATCH$_ENTMODIFIED, BATCH$_NOSUCHENT, BATCH$_NOPRIV, BATCH$_NOTMODIFIABLE, BATCH$_NOTHINGSET,
**	BATCH$_IVPRIO, BATCH$_IVCPU or BATCH$_LOGSPEC (a name that cannot serve as one).
**--
*/
static	int	s_batchd$do_setent (
			unsigned int		a_entry,
			int			a_uid,
			const BATCH$SETENT *	a_req
			)
{
BATCHD$JOBENT *	l_ent;
BATCH$JOB *	l_job;
cpu_set_t	l_cpus;
const char *	l_state;
long long	l_now;
char		l_what [BATCH$K_NAMESZ + BATCH$K_CPUSZ + 160];
int		l_ncpu;

	if ( $IS_FAILURE(s_batchd$job_by_entry(a_entry, &l_ent)) )
		return	$PUTMSG_FAO(BATCH$_NOSUCHENT, a_entry);

	l_job = &l_ent->je_job;

	if ( (a_uid) && (l_job->jb_uid != a_uid) )
		return	$PUTMSG_FAO(BATCH$_NOPRIV, a_entry, l_job->jb_uid);

	if ( !a_req->se_mask )
		return	$PUTMSG_FAO(BATCH$_NOTHINGSET);

	util$state_name(l_job->jb_state, &l_state);

	/* What the state of the job allows to be altered */
	switch ( l_job->jb_state )
		{
		case	JOB$K_PENDING:
		case	JOB$K_HOLDING:
			break;

		case	JOB$K_EXECUTING:
		case	JOB$K_ABORTING:
			if ( a_req->se_mask & ~SETENT$M_WHILERUN )
				return	$PUTMSG_FAO(BATCH$_NOTMODIFIABLE, a_entry, l_state);
			break;

		default:
			return	$PUTMSG_FAO(BATCH$_NOTMODIFIABLE, a_entry, l_state);
		}

	/* The values, before any of them is applied */
	if ( (a_req->se_mask & SETENT$M_PRIORITY)
		&& ((a_req->se_prio < 1) || (a_req->se_prio > BATCH$K_MAXPRIO)) )
		return	$PUTMSG_FAO(BATCH$_IVPRIO, BATCH$K_MAXPRIO);

	if ( (a_req->se_mask & SETENT$M_NAME)
		&& ((!a_req->se_name[0]) || (strchr(a_req->se_name, '/')) || (strstr(a_req->se_name, ".."))) )
		return	$PUTMSG_FAO(BATCH$_LOGSPEC);

	if ( (a_req->se_mask & SETENT$M_CPU) && (a_req->se_cpus[0])
		&& ($IS_FAILURE(util$parse_cpulist(a_req->se_cpus, &l_cpus, &l_ncpu))) )
		return	$PUTMSG_FAO(BATCH$_IVCPU, a_req->se_cpus);

	l_now = (long long) time(NULL);

	if ( a_req->se_mask & SETENT$M_PRIORITY )
		l_job->jb_prio = a_req->se_prio;

	if ( a_req->se_mask & SETENT$M_NAME )
		util$mkfspec(l_job->jb_name, sizeof(l_job->jb_name), "!AZ", (const fao_prm_t []) { (fao_prm_t) a_req->se_name }, 1);

	if ( a_req->se_mask & SETENT$M_RETAIN )
		l_job->jb_retain = a_req->se_retain;

	if ( a_req->se_mask & SETENT$M_KEEP )
		l_job->jb_keeplog = (a_req->se_keep != 0);

	if ( a_req->se_mask & SETENT$M_RESTART )
		l_job->jb_restart = (a_req->se_restart != 0);

	if ( a_req->se_mask & SETENT$M_CPU )
		util$mkfspec(l_job->jb_cpus, sizeof(l_job->jb_cpus), "!AZ", (const fao_prm_t []) { (fao_prm_t) a_req->se_cpus }, 1);

	/*
	**  Holding first, then the time, so that /NOHOLD /AFTER=t lets the job go and then withholds
	**  it until t, which is what the two say when read in order.
	*/
	if ( a_req->se_mask & SETENT$M_HOLD )
		{
		if ( (a_req->se_hold) && (l_job->jb_state == JOB$K_PENDING) )
			l_job->jb_state = JOB$K_HOLDING;
		else if ( (!a_req->se_hold) && (l_job->jb_state == JOB$K_HOLDING) )
			{
			l_job->jb_state	= JOB$K_PENDING;
			l_job->jb_after	= 0;
			}
		}

	if ( a_req->se_mask & SETENT$M_AFTER )
		{
		if ( (!a_req->se_after) && (l_job->jb_state == JOB$K_HOLDING) && (l_job->jb_after)
			&& (!((a_req->se_mask & SETENT$M_HOLD) && (a_req->se_hold))) )
			l_job->jb_state = JOB$K_PENDING;

		l_job->jb_after = a_req->se_after;

		if ( (l_job->jb_after > l_now) && (l_job->jb_state == JOB$K_PENDING) )
			l_job->jb_state = JOB$K_HOLDING;
		}

	util$fmt_setent(a_req, l_what, sizeof(l_what));

	return	$PUTMSG_FAO(BATCH$_ENTMODIFIED, a_entry, l_what);
}

/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Decides whether a job belongs in the selection a request calls for. The rule is stated in one
**	place because the queue is walked twice - once to count the entries, once to transmit them -
**	and the two passes have to agree.
**
**  INPUT PARAMETERS:
**	a_job	- job descriptor, not NULL.
**	a_req	- the parsed selection request, not NULL.
**	a_uid	- user identifier of the sender, obtained from SO_PEERCRED.
**
**  OUTPUT PARAMETERS:
**	None.
**
**  RETURNS:
**	BATCH$_NORMAL if the job belongs in the selection, otherwise BATCH$_NOSUCHENT.
**--
*/
static	int	s_batchd$job_selected (
			const BATCH$JOB *	a_job,
			BATCH$SHOWREQ *		a_req,
			int			a_uid
			)
{
unsigned int	l_st;

	l_st = a_job->jb_state;

	/*
	**  The privileged user is not filtered by ownership. He may delete and release any entry - the
	**  check that governs those operations says so - and an entry he may dispose of but cannot see
	**  is worse than useless. It became plain once /USER existed: a job submitted by the privileged
	**  user to run as somebody else belongs to that somebody at once, and its submitter could not
	**  see the entry he had just been given the number of.
	*/
	if ( (a_uid) && (!(a_req->sr_flags & SHOW$M_EVERYONE)) && (a_job->jb_uid != a_uid) )
		return	BATCH$_NOSUCHENT;

	if ( (a_req->sr_queue[0]) && (strcmp(a_req->sr_queue, a_job->jb_queue)) )
		return	BATCH$_NOSUCHENT;

	/*
	**  An entry named by the enquirer stands on its own: it is displayed whatever state it is in,
	**  so that SHOW of an entry does not depend upon /ALL as SHOW of the queue does.
	*/
	if ( a_req->sr_entry )
		return	(a_job->jb_entry == a_req->sr_entry) ? BATCH$_NORMAL : BATCH$_NOSUCHENT;

	/*
	**  Without /ALL the queue is shown as it stands to be worked: what runs, what waits to run and
	**  what failed. The one thing left out is an entry kept after a job that succeeded - it is there
	**  only because /RETAIN=ALWAYS asked for it, and there is nothing in it to be acted upon.
	*/
	if ( (!(a_req->sr_flags & SHOW$M_ALL)) && (l_st == JOB$K_RETAINED) && (!a_job->jb_exit) && (!a_job->jb_signal) )
		return	BATCH$_NOSUCHENT;

	return	BATCH$_NORMAL;
}

/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Builds the reply to an IPC$K_SHOW request in memory. The selection rule is:
**
**	  without /ALL : PENDING, HOLDING, EXECUTING, ABORTING and the RETAINED entries of the jobs that
**			 failed, that is, the queue as it stands to be worked;
**	  with    /ALL : the RETAINED entries of the jobs that succeeded (/RETAIN=ALWAYS) in addition.
**
**	Jobs of other users are supplied only when the SHOW$M_EVERYONE indicator is set. The full
**	format /FULL has no effect on the selection: the controller always supplies the complete
**	descriptor and the utility chooses how much of it to display.
**
**	The reply is assembled whole rather than written out as the queue is walked. It is transmitted
**	afterwards, over as many turns of the main loop as the client needs to take it, and the queue
**	may change in the meantime: what the client receives is the queue as it stood when it asked.
**
**  INPUT PARAMETERS:
**	a_req	- the parsed selection request, not NULL.
**	a_uid	- user identifier of the sender, obtained from SO_PEERCRED.
**
**  OUTPUT PARAMETERS:
**	a_buf	- receives the address of the reply, not NULL. The caller releases it.
**	a_len	- receives the length of the reply in bytes, not NULL.
**
**  RETURNS:
**	BATCH$_NORMAL or BATCH$_NOSLOT if the reply cannot be held.
**--
*/
static	int	s_batchd$do_show (
			BATCH$SHOWREQ *	a_req,
			int		a_uid,
			char **		a_buf,
			unsigned int *	a_len
			)
{
BATCHD$JOBENT *	l_ent;
BATCH$IPCHDR	l_hdr;
char *		l_buf;
unsigned int	l_len;
int		l_n;

	*a_buf	= NULL;
	*a_len	= 0;
	l_n	= 0;

	for ( l_ent = $JOBQUE_FIRST(); l_ent; l_ent = $JOBQUE_NEXT(l_ent) )
		{
		if ( $IS_SUCCESS(s_batchd$job_selected(&l_ent->je_job, a_req, a_uid)) )
			l_n++;
		}

	l_len = (unsigned int) (sizeof(l_hdr) + (size_t) l_n * sizeof(BATCH$JOB));

	if ( !(l_buf = calloc(1, l_len)) )
		return	$PUTMSG_FAO(BATCH$_NOSLOT, BATCH$K_MAXJOB, "assembling a SHOW reply");

	memset(&l_hdr, 0, sizeof(l_hdr));
	l_hdr.mh_magic	= IPC$K_MAGIC;
	l_hdr.mh_func	= IPC$K_SHOW;
	l_hdr.mh_sts	= BATCH$_NORMAL;
	l_hdr.mh_count	= (unsigned int) l_n;

	/*
	**  The counter of entry numbers travels with the reply. It is what lets an enquirer tell an entry
	**  that has been and gone from one that was never issued - the queue itself cannot say, an entry
	**  leaving it without trace when its job succeeds.
	*/
	l_hdr.mh_entry	= g_entry;
	l_hdr.mh_len	= (unsigned int) ((size_t) l_n * sizeof(BATCH$JOB));

	memcpy(l_buf, &l_hdr, sizeof(l_hdr));
	l_len = (unsigned int) sizeof(l_hdr);

	for ( l_ent = $JOBQUE_FIRST(); l_ent; l_ent = $JOBQUE_NEXT(l_ent) )
		{
		if ( $IS_FAILURE(s_batchd$job_selected(&l_ent->je_job, a_req, a_uid)) )
			continue;

		memcpy(l_buf + l_len, &l_ent->je_job, sizeof(BATCH$JOB));
		l_len += (unsigned int) sizeof(BATCH$JOB);
		}

	*a_buf	= l_buf;
	*a_len	= l_len;

	return	BATCH$_NORMAL;
}

/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Releases a connection: closes its descriptor, takes it out of the queue of connections and
**	frees what it held.
**
**  INPUT PARAMETERS:
**	a_conn	- the connection, not NULL. It is not to be referred to afterwards.
**
**  OUTPUT PARAMETERS:
**	None.
**
**  RETURNS:
**	BATCH$_NORMAL.
**--
*/
static	int	s_batchd$conn_close (
			BATCHD$CONN *	a_conn
			)
{
unsigned	l_count;

	if ( a_conn->cn_fd != -1 )
		close(a_conn->cn_fd);

	$REMQENT(&g_connque, a_conn, &l_count);
	free(a_conn->cn_buf);
	free(a_conn);

	return	BATCH$_NORMAL;
}

/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Accepts a connection and places it in the queue of connections, ready to be read.
**
**	The descriptor is made non-blocking: everything this module does with a connection thereafter
**	is one step of a transfer, taken when the descriptor is ready and never waited upon. A client
**	that connects and then says nothing therefore costs a descriptor and nothing else.
**
**	The identity of the sender is taken from the kernel here, once, and kept for the life of the
**	connection.
**
**  INPUT PARAMETERS:
**	a_lfd	- descriptor of the listening socket.
**
**  OUTPUT PARAMETERS:
**	None.
**
**  RETURNS:
**	BATCH$_NORMAL, BATCH$_ACCEPTERR, BATCH$_PEERCRED or BATCH$_NOSLOT.
**--
*/
static	int	s_batchd$conn_accept (
			int	a_lfd
			)
{
BATCHD$CONN *	l_conn;
struct ucred	l_cred;
socklen_t	l_credsz;
unsigned	l_count;
int		l_fd, l_rc;

	if ( 0 > (l_fd = accept(a_lfd, NULL, NULL)) )
		{
		if ( (errno == EINTR) || (errno == EAGAIN) || (errno == EWOULDBLOCK) )
			return	BATCH$_NORMAL;

		return	$PUTMSG_FAO(BATCH$_ACCEPTERR, l_fd, errno, strerror(errno));
		}

	/*
	**  A client beyond the ceiling is turned away at once rather than left waiting: the controller
	**  would otherwise stop accepting altogether and give no sign of why.
	*/
	if ( g_connque.count >= BATCHD$K_MAXCONN )
		{
		close(l_fd);

		return	$PUTMSG_FAO(BATCH$_NOSLOT, BATCHD$K_MAXCONN, "accepting a connection");
		}

	l_credsz = sizeof(l_cred);
	memset(&l_cred, 0, sizeof(l_cred));

	if ( 0 > (l_rc = getsockopt(l_fd, SOL_SOCKET, SO_PEERCRED, &l_cred, &l_credsz)) )
		{
		close(l_fd);

		return	$PUTMSG_FAO(BATCH$_PEERCRED, l_rc, errno, strerror(errno));
		}

	if ( !(l_conn = calloc(1, sizeof(BATCHD$CONN))) )
		{
		close(l_fd);

		return	$PUTMSG_FAO(BATCH$_NOSLOT, BATCHD$K_MAXCONN, "accepting a connection");
		}

	fcntl(l_fd, F_SETFL, O_NONBLOCK);

	l_conn->cn_fd		= l_fd;
	l_conn->cn_uid		= (int) l_cred.uid;
	l_conn->cn_gid		= (int) l_cred.gid;
	l_conn->cn_state	= CONN$K_RDHDR;
	l_conn->cn_len		= (unsigned int) sizeof(l_conn->cn_hdr);
	l_conn->cn_done		= 0;
	l_conn->cn_deadline	= (long long) time(NULL) + BATCHD$K_CONNTMO;

	if ( $IS_FAILURE($INSQTAIL(&g_connque, l_conn, &l_count)) )
		{
		close(l_fd);
		free(l_conn);

		return	$PUTMSG_FAO(BATCH$_NOSLOT, BATCHD$K_MAXCONN, "accepting a connection");
		}

	return	BATCH$_NORMAL;
}



/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Removes the entries of jobs that have finished.
**
**	Only an entry in the RETAINED state is taken: that is what a job which has finished leaves
**	behind, and nothing else in the queue is finished with. A job still executing is not touched -
**	PURGE is a tidying of what is over, not a way of stopping what is not.
**
**	The entries of others are passed over unless the caller is the privileged user. He may dispose
**	of any entry, and a purge that left his own untouched among the rest would be a surprise.
**
**  INPUT PARAMETERS:
**	a_uid	- identity of the sender, as the kernel reports it.
**	a_queue	- restrict to this queue, or an empty string for all of them.
**
**  OUTPUT PARAMETERS:
**	a_count	- receives the number removed.
**
**  RETURNS:
**	BATCH$_NORMAL.
**--
*/
static	int	s_batchd$do_purge (
			int		a_uid,
			const char *	a_queue,
			unsigned *	a_count
			)
{
BATCHD$JOBENT *	l_ent, *l_next;
unsigned	l_n = 0;

	for ( l_ent = $JOBQUE_FIRST(); l_ent; l_ent = l_next )
		{
		l_next = $JOBQUE_NEXT(l_ent);

		if ( l_ent->je_job.jb_state != JOB$K_RETAINED )
			continue;

		if ( (a_uid) && (l_ent->je_job.jb_uid != a_uid) )
			continue;

		if ( (a_queue[0]) && (strcasecmp(l_ent->je_job.jb_queue, a_queue)) )
			continue;

		s_batchd$job_free(l_ent);

		l_n++;
		}

	*a_count = l_n;

	return	BATCH$_NORMAL;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Assembles the state of the job controller itself for an IPC$K_MGRSTAT request.
**
**	It answers what a display of the queue cannot: whether anybody is minding that queue, since
**	when, on what terms, and with what left to give. A queue that stands still looks the same
**	whether the controller is busy, idle or gone.
**
**  INPUT PARAMETERS:
**	None; everything is taken from the state of this process.
**
**  OUTPUT PARAMETERS:
**	a_buf	- receives an allocated reply, header and record; the caller owns it.
**	a_len	- receives its length.
**
**  RETURNS:
**	BATCH$_NORMAL, or BATCH$_NOSLOT where the reply cannot be allocated.
**--
*/
static	int	s_batchd$do_mgrstat (
			char **		a_buf,
			unsigned *	a_len
			)
{
BATCH$IPCHDR	l_hdr;
BATCH$MGRSTAT	l_st;
BATCHD$JOBENT *	l_ent;
char *		l_buf;

	memset(&l_st, 0, sizeof(l_st));

	l_st.ms_pid	= (unsigned int) getpid();
	l_st.ms_joblim	= (unsigned int) g_joblim;
	l_st.ms_entry	= g_entry;
	l_st.ms_logkeep	= (unsigned int) g_logkeep;
	l_st.ms_stime	= g_stime;
	l_st.ms_lcorecnt = (unsigned int) g_lcorecnt;

	for ( l_ent = $JOBQUE_FIRST(); l_ent; l_ent = $JOBQUE_NEXT(l_ent) )
		{
		switch ( l_ent->je_job.jb_state )
			{
			case	JOB$K_EXECUTING:	l_st.ms_executing++;	break;
			case	JOB$K_PENDING:		l_st.ms_pending++;	break;
			case	JOB$K_HOLDING:		l_st.ms_holding++;	break;
			case	JOB$K_ABORTING:		l_st.ms_aborting++;	break;
			case	JOB$K_RETAINED:		l_st.ms_retained++;	break;
			}
		}

	util$mkfspec(l_st.ms_ident, sizeof(l_st.ms_ident), "!AZ",
			(const fao_prm_t []) { (fao_prm_t) __IDENT__ }, 1);
	util$mkfspec(l_st.ms_version, sizeof(l_st.ms_version), "!AZ",
			(const fao_prm_t []) { (fao_prm_t) BATCH$K_VERSION }, 1);
	util$mkfspec(l_st.ms_queue, sizeof(l_st.ms_queue), "!AZ",
			(const fao_prm_t []) { (fao_prm_t) g_queue }, 1);
	util$mkfspec(l_st.ms_root, sizeof(l_st.ms_root), "!AZ",
			(const fao_prm_t []) { (fao_prm_t) g_root }, 1);
	util$mkfspec(l_st.ms_lcore, sizeof(l_st.ms_lcore), "!AZ",
			(const fao_prm_t []) { (fao_prm_t) g_lcorestr }, 1);

	/* The queues, each with the counts of its own */
	l_st.ms_quecnt = (unsigned int) g_quecnt;

	for ( int q = 0; q < g_quecnt; q++ )
		{
		util$mkfspec(l_st.ms_queues[q].qs_name, sizeof(l_st.ms_queues[0].qs_name), "!AZ",
				(const fao_prm_t []) { (fao_prm_t) g_queues[q].qe_name }, 1);

		l_st.ms_queues[q].qs_joblim	= (unsigned int) g_queues[q].qe_joblim;
		l_st.ms_queues[q].qs_paused	= (unsigned int) g_queues[q].qe_paused;

		for ( l_ent = $JOBQUE_FIRST(); l_ent; l_ent = $JOBQUE_NEXT(l_ent) )
			{
			if ( strcasecmp(l_ent->je_job.jb_queue, g_queues[q].qe_name) )
				continue;

			switch ( l_ent->je_job.jb_state )
				{
				case	JOB$K_EXECUTING:
				case	JOB$K_ABORTING:		l_st.ms_queues[q].qs_executing++;	break;
				case	JOB$K_PENDING:		l_st.ms_queues[q].qs_pending++;		break;
				case	JOB$K_HOLDING:		l_st.ms_queues[q].qs_holding++;		break;
				}
			}
		}

	if ( !(l_buf = calloc(1, sizeof(l_hdr) + sizeof(l_st))) )
		return	$PUTMSG_FAO(BATCH$_NOSLOT, 1, "the state of the controller");

	memset(&l_hdr, 0, sizeof(l_hdr));

	l_hdr.mh_magic	= IPC$K_MAGIC;
	l_hdr.mh_func	= IPC$K_MGRSTAT;
	l_hdr.mh_sts	= BATCH$_NORMAL;
	l_hdr.mh_count	= 1;
	l_hdr.mh_len	= (unsigned int) sizeof(l_st);

	memcpy(l_buf, &l_hdr, sizeof(l_hdr));
	memcpy(l_buf + sizeof(l_hdr), &l_st, sizeof(l_st));

	*a_buf	= l_buf;
	*a_len	= (unsigned) (sizeof(l_hdr) + sizeof(l_st));

	return	BATCH$_NORMAL;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Acts upon a request whose header and payload have both arrived, and prepares the reply.
**
**	Every reply is assembled in memory before any of it is transmitted, so that the queue of jobs
**	is not held open across a transfer to a client that may be slow.
**
**  INPUT PARAMETERS:
**	a_conn	- the connection, not NULL.
**
**  OUTPUT PARAMETERS:
**	a_dirty	- receives an indication that the queue of jobs has changed, not NULL.
**
**  RETURNS:
**	BATCH$_NORMAL, or a condition if the reply could not be prepared.
**--
*/
static	int	s_batchd$conn_dispatch (
			BATCHD$CONN *	a_conn,
			int *		a_dirty
			)
{
BATCHD$JOBENT *	l_ent;
BATCH$IPCHDR	l_rsp;
char *		l_buf = NULL;
unsigned int	l_len;
int		l_sts;

	*a_dirty = 0;

	memset(&l_rsp, 0, sizeof(l_rsp));
	l_rsp.mh_magic	= IPC$K_MAGIC;
	l_rsp.mh_func	= a_conn->cn_hdr.mh_func;
	l_rsp.mh_sts	= BATCH$_BUGCHECK;

	switch ( a_conn->cn_hdr.mh_func )
		{
		case	IPC$K_SUBMIT:
			l_sts		= s_batchd$do_submit((BATCH$JOB *) a_conn->cn_buf, a_conn->cn_uid,
								a_conn->cn_gid, &l_rsp.mh_entry);
			l_rsp.mh_sts	= (unsigned int) l_sts;
			*a_dirty	= $IS_SUCCESS(l_sts);
			break;

		case	IPC$K_DELETE:
			l_sts		= s_batchd$do_delete(a_conn->cn_hdr.mh_entry, a_conn->cn_uid);
			l_rsp.mh_sts	= (unsigned int) l_sts;
			l_rsp.mh_entry	= a_conn->cn_hdr.mh_entry;
			*a_dirty	= $IS_SUCCESS(l_sts);
			break;

		case	IPC$K_RELEASE:
			l_rsp.mh_entry = a_conn->cn_hdr.mh_entry;

			if ( $IS_FAILURE(s_batchd$job_by_entry(a_conn->cn_hdr.mh_entry, &l_ent)) )
				{
				l_rsp.mh_sts = (unsigned int) $PUTMSG_FAO(BATCH$_NOSUCHENT,
							a_conn->cn_hdr.mh_entry);
				break;
				}

			if ( l_ent->je_job.jb_state != JOB$K_HOLDING )
				{
				l_rsp.mh_sts = (unsigned int) $PUTMSG_FAO(BATCH$_NOTHELD,
							a_conn->cn_hdr.mh_entry);
				break;
				}

			/* The /AFTER time is cleared, else the scheduler would withhold the job again */
			l_ent->je_job.jb_state	= JOB$K_PENDING;
			l_ent->je_job.jb_after	= 0;
			*a_dirty		= 1;
			l_rsp.mh_sts		= (unsigned int) $PUTMSG_FAO(BATCH$_ENTRELEASED,
							a_conn->cn_hdr.mh_entry);
			break;

		case	IPC$K_SETENTRY:
			/* A header alone is a request with nothing in it, and is not looked into */
			if ( !a_conn->cn_buf )
				{
				l_rsp.mh_sts = (unsigned int) $PUTMSG_FAO(BATCH$_BADLENGTH, "the", a_conn->cn_hdr.mh_len);
				break;
				}

			l_sts		= s_batchd$do_setent(a_conn->cn_hdr.mh_entry, a_conn->cn_uid,
								(const BATCH$SETENT *) a_conn->cn_buf);
			l_rsp.mh_sts	= (unsigned int) l_sts;
			l_rsp.mh_entry	= a_conn->cn_hdr.mh_entry;
			*a_dirty	= $IS_SUCCESS(l_sts);
			break;

		case	IPC$K_QUEUEOP:
			{
			BATCH$QUEOP *	l_op = (BATCH$QUEOP *) a_conn->cn_buf;
			BATCHD$QUEUE *	l_q;
			char		l_qn [BATCH$K_QUESZ];

			util$mkfspec(l_qn, sizeof(l_qn), "!AZ", (const fao_prm_t []) { (fao_prm_t) l_op->qo_name }, 1);

			/*
			**  Pausing a queue withholds the work of everybody who uses it, so it is granted to
			**  the privileged user alone - ownership of a job says nothing about the queue it
			**  waits in.
			*/
			if ( a_conn->cn_uid )
				{
				l_rsp.mh_sts = (unsigned int) $PUTMSG_FAO(BATCH$_NOPRIVUSER, a_conn->cn_uid, l_qn);
				break;
				}

			if ( $IS_FAILURE(s_batchd$que_by_name(l_qn, &l_q)) )
				{
				l_rsp.mh_sts = (unsigned int) $PUTMSG_FAO(BATCH$_NOSUCHQUE, l_qn);
				break;
				}

			l_q->qe_paused	= (l_op->qo_action == QUEOP$K_PAUSE);
			l_rsp.mh_sts	= (unsigned int) $PUTMSG_FAO((l_q->qe_paused) ? BATCH$_QUEPAUSED
									: BATCH$_QUERESUMED, l_qn);
			*a_dirty	= 1;
			}
			break;

		case	IPC$K_PURGE:
			{
			BATCH$SHOWREQ *	l_req = (BATCH$SHOWREQ *) a_conn->cn_buf;
			unsigned	l_purged = 0;

			s_batchd$do_purge(a_conn->cn_uid, l_req->sr_queue, &l_purged);

			l_rsp.mh_count	= l_purged;
			l_rsp.mh_sts	= (unsigned int) BATCH$_NORMAL;
			*a_dirty	= (l_purged != 0);
			}
			break;

		case	IPC$K_MGRSTAT:
			l_sts = s_batchd$do_mgrstat(&l_buf, &l_len);

			if ( $IS_SUCCESS(l_sts) )
				{
				free(a_conn->cn_buf);
				a_conn->cn_buf		= l_buf;
				a_conn->cn_len		= l_len;
				a_conn->cn_done		= 0;
				a_conn->cn_state	= CONN$K_WRRSP;

				return	BATCH$_NORMAL;
				}

			l_rsp.mh_sts = (unsigned int) l_sts;
			break;

		case	IPC$K_SHOW:
			l_sts = s_batchd$do_show((BATCH$SHOWREQ *) a_conn->cn_buf, a_conn->cn_uid,
							&l_buf, &l_len);

			if ( $IS_SUCCESS(l_sts) )
				{
				free(a_conn->cn_buf);
				a_conn->cn_buf		= l_buf;
				a_conn->cn_len		= l_len;
				a_conn->cn_done		= 0;
				a_conn->cn_state	= CONN$K_WRRSP;

				return	BATCH$_NORMAL;
				}

			l_rsp.mh_sts = (unsigned int) l_sts;
			break;

		default:
			l_rsp.mh_sts = (unsigned int) $PUTMSG_FAO(BATCH$_BADFUNC, a_conn->cn_hdr.mh_func);
			break;
		}

	/*
	**  Every function but SHOW answers with a header alone.
	*/
	free(a_conn->cn_buf);

	if ( !(a_conn->cn_buf = calloc(1, sizeof(l_rsp))) )
		return	$PUTMSG_FAO(BATCH$_NOSLOT, BATCHD$K_MAXCONN, "preparing a reply");

	memcpy(a_conn->cn_buf, &l_rsp, sizeof(l_rsp));

	a_conn->cn_len		= (unsigned int) sizeof(l_rsp);
	a_conn->cn_done		= 0;
	a_conn->cn_state	= CONN$K_WRRSP;

	return	BATCH$_NORMAL;
}

/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Takes the next step of reading a request. One call transfers whatever the descriptor holds and
**	no more; the header and the payload are gathered over as many calls as it takes.
**
**  INPUT PARAMETERS:
**	a_conn	- the connection, not NULL.
**
**  OUTPUT PARAMETERS:
**	a_dirty	- receives an indication that the queue of jobs has changed, not NULL.
**
**  RETURNS:
**	BATCH$_NORMAL while the connection is to be kept, otherwise a condition and the connection is
**	to be released.
**--
*/
static	int	s_batchd$conn_read (
			BATCHD$CONN *	a_conn,
			int *		a_dirty
			)
{
char *	l_dst;
ssize_t	l_rc;

	*a_dirty	= 0;
	l_dst		= (a_conn->cn_state == CONN$K_RDHDR) ? (char *) &a_conn->cn_hdr : a_conn->cn_buf;
	l_rc		= read(a_conn->cn_fd, l_dst + a_conn->cn_done, a_conn->cn_len - a_conn->cn_done);

	if ( (l_rc == -1) && ((errno == EAGAIN) || (errno == EWOULDBLOCK) || (errno == EINTR)) )
		return	BATCH$_NORMAL;

	if ( !l_rc )
		return	$PUTMSG_FAO(BATCH$_CONNCLOSED, a_conn->cn_uid);

	if ( l_rc < 0 )
		return	$PUTMSG_FAO(BATCH$_IOERR, "read", (int) l_rc, errno, strerror(errno));

	a_conn->cn_done		+= (unsigned int) l_rc;
	a_conn->cn_deadline	= (long long) time(NULL) + BATCHD$K_CONNTMO;

	if ( a_conn->cn_done < a_conn->cn_len )
		return	BATCH$_NORMAL;

	/*
	**  The header is complete. Its signature is verified and, where the function carries a payload,
	**  room is made for it; otherwise the request is acted upon at once.
	*/
	if ( a_conn->cn_state == CONN$K_RDHDR )
		{
		if ( a_conn->cn_hdr.mh_magic != IPC$K_MAGIC )
			return	$PUTMSG_FAO(BATCH$_BADMAGIC, a_conn->cn_hdr.mh_magic);

		if ( a_conn->cn_hdr.mh_len )
			{
			if ( ((a_conn->cn_hdr.mh_func == IPC$K_PURGE) && (a_conn->cn_hdr.mh_len != sizeof(BATCH$SHOWREQ)))
				|| ((a_conn->cn_hdr.mh_func == IPC$K_QUEUEOP) && (a_conn->cn_hdr.mh_len != sizeof(BATCH$QUEOP)))
				|| ((a_conn->cn_hdr.mh_func == IPC$K_SETENTRY) && (a_conn->cn_hdr.mh_len != sizeof(BATCH$SETENT)))
				|| ((a_conn->cn_hdr.mh_func == IPC$K_SUBMIT) && (a_conn->cn_hdr.mh_len != sizeof(BATCH$JOB)))
				|| ((a_conn->cn_hdr.mh_func == IPC$K_SHOW) && (a_conn->cn_hdr.mh_len != sizeof(BATCH$SHOWREQ))) )
				return	$PUTMSG_FAO(BATCH$_BADLENGTH, "the", a_conn->cn_hdr.mh_len);

			if ( !(a_conn->cn_buf = calloc(1, a_conn->cn_hdr.mh_len)) )
				return	$PUTMSG_FAO(BATCH$_NOSLOT, BATCHD$K_MAXCONN, "receiving a request");

			a_conn->cn_len		= a_conn->cn_hdr.mh_len;
			a_conn->cn_done		= 0;
			a_conn->cn_state	= CONN$K_RDPLD;

			return	BATCH$_NORMAL;
			}
		}

	return	s_batchd$conn_dispatch(a_conn, a_dirty);
}

/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Takes the next step of transmitting a reply.
**
**  INPUT PARAMETERS:
**	a_conn	- the connection, not NULL.
**
**  OUTPUT PARAMETERS:
**	None.
**
**  RETURNS:
**	BATCH$_NORMAL while the connection is to be kept, BATCH$_ENDOFDATA once the reply has gone in
**	full, otherwise a condition; in either of the latter two the connection is to be released.
**--
*/
static	int	s_batchd$conn_write (
			BATCHD$CONN *	a_conn
			)
{
ssize_t	l_rc;

	l_rc = write(a_conn->cn_fd, a_conn->cn_buf + a_conn->cn_done, a_conn->cn_len - a_conn->cn_done);

	if ( (l_rc == -1) && ((errno == EAGAIN) || (errno == EWOULDBLOCK) || (errno == EINTR)) )
		return	BATCH$_NORMAL;

	if ( l_rc <= 0 )
		return	$PUTMSG_FAO(BATCH$_IOERR, "write", (int) l_rc, errno, strerror(errno));

	a_conn->cn_done		+= (unsigned int) l_rc;
	a_conn->cn_deadline	= (long long) time(NULL) + BATCHD$K_CONNTMO;

	return	(a_conn->cn_done < a_conn->cn_len) ? BATCH$_NORMAL : BATCH$_ENDOFDATA;
}

/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Creates the listening AF_UNIX socket. The socket file is given mode 0666: access to jobs is
**	regulated by checking the identification of the sender in the handlers rather than by file
**	system permissions, so anyone is permitted to connect.
**
**  INPUT PARAMETERS:
**	a_pathsz - size of the buffer a_path in bytes.
**
**  OUTPUT PARAMETERS:
**	a_path	- receives the actual specification of the socket file, not NULL.
**	a_fd	- receives the descriptor of the listening socket, not NULL. On error the value -1.
**
**  RETURNS:
**	BATCH$_NORMAL, BATCH$_SOCKPATH, BATCH$_SOCKERR, BATCH$_BINDERR or BATCH$_LISTENERR.
**--
*/
static	int	s_batchd$listen (
			char *	a_path,
			size_t	a_pathsz,
			int *	a_fd
			)
{
struct sockaddr_un	l_sa;
int			l_fd, l_rc, l_live, l_i;

	*a_fd = -1;

	if ( $IS_FAILURE(util$mkfspec(a_path, a_pathsz, "!AZ/!AZ",
				(const fao_prm_t []) { (fao_prm_t) g_root, (fao_prm_t) IPC$K_SOCKNAME }, 2)) )
		return	BATCH$_SOCKPATH;

	memset(&l_sa, 0, sizeof(l_sa));
	l_sa.sun_family = AF_UNIX;

	/* The sun_path field is shorter than the usual path limit, so truncation here is fatal */
	if ( $IS_FAILURE(util$mkfspec(l_sa.sun_path, sizeof(l_sa.sun_path), "!AZ",
				(const fao_prm_t []) { (fao_prm_t) a_path }, 1)) )
		return	$PUTMSG_FAO(BATCH$_SOCKPATH, a_path, sizeof(l_sa.sun_path));

	/*
	**  A socket file left over from a previous run would prevent the bind, so it is removed - but
	**  only after asking whether anyone is listening on it. Removing it blindly would let a second
	**  controller take the root away from a living one, and the two would then write one journal
	**  in turn, each unaware of the other's queue.
	*/
	/*
	**  A controller that has been signalled holds its socket until it has finished stopping, so a
	**  single refusal to connect settles nothing: "pkill batchd && batchd" is an ordinary way to
	**  restart the thing, and it would fail as often as not. The answer is sought for a few
	**  seconds before it is believed.
	*/
	for ( l_i = 0, l_live = 1; (l_live) && (l_i < BATCHD$K_INUSETRY); l_i++ )
		{
		if ( 0 > (l_rc = socket(AF_UNIX, SOCK_STREAM, 0)) )
			{
			l_live = 0;
			break;
			}

		l_live = (0 == connect(l_rc, (struct sockaddr *) &l_sa, sizeof(l_sa)));

		close(l_rc);

		if ( l_live )
			usleep(BATCHD$K_INUSEWAIT);
		}

	if ( l_live )
		return	$PUTMSG_FAO(BATCH$_INUSE, a_path);

	unlink(a_path);

	if ( 0 > (l_fd = socket(AF_UNIX, SOCK_STREAM, 0)) )
		return	$PUTMSG_FAO(BATCH$_SOCKERR, l_fd, errno, strerror(errno));

	if ( 0 > (l_rc = bind(l_fd, (struct sockaddr *) &l_sa, sizeof(l_sa))) )
		{
		close(l_fd);

		return	$PUTMSG_FAO(BATCH$_BINDERR, a_path, l_rc, errno, strerror(errno));
		}

	chmod(a_path, 0666);

	if ( 0 > (l_rc = listen(l_fd, BATCHD$K_LISTENQ)) )
		{
		close(l_fd);

		return	$PUTMSG_FAO(BATCH$_LISTENERR, a_path, l_rc, errno, strerror(errno));
		}

	*a_fd = l_fd;

	return	BATCH$_NORMAL;
}

/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Says on the terminal what became of the attempt to start, and where the rest is to be read.
**
**	The diagnostics of the controller go to its own log from the moment it starts, so a terminal
**	which is about to be let go would otherwise be told nothing at all - the command would return
**	in silence whether the controller had started or not.
**
**	The standard error is written to directly rather than through the FAO engine: that engine
**	writes to the standard output, and this is a remark about the running of the program rather
**	than a part of what it was run to produce.
**
**  INPUT PARAMETERS:
**	a_log	- specification of the log, not NULL.
**	a_ok	- non-zero where the controller is starting.
**
**  OUTPUT PARAMETERS:
**	None.
**
**  RETURNS:
**	BATCH$_NORMAL.
**--
*/
static	int	s_batchd$announce (
			const char *	a_log,
			int		a_ok
			)
{
char		l_buf [BATCH$K_PATHSZ + 128];
unsigned short	l_len = 0;

	util$mkfspec(l_buf, sizeof(l_buf), "!AZ, root !AZ, log !AZ\n",
			(const fao_prm_t []) { (fao_prm_t) ((a_ok) ? "%BATCH-S-CTLSTARTED, job controller started"
								: "%BATCH-F-CTLNOSTART, job controller did not start"),
						(fao_prm_t) g_root, (fao_prm_t) a_log }, 3);

	l_len = (unsigned short) strlen(l_buf);

	/* Best effort: the terminal may already be gone, and the log has the whole of it */
	if ( l_len != write(STDERR_FILENO, l_buf, l_len) )
		{ }

	return	BATCH$_NORMAL;
}


/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Entry point of the job controller. It parses the options, prepares the working directories,
**	restores the queue from the journal, creates the listening socket and enters the main service
**	loop.
**
**	On each turn the main loop collects the status of terminated jobs, reschedules the queue and,
**	if anything changed, writes the journal, after which it waits for an event on the listening
**	socket or on the self-pipe that delivers signals. The upper bound on the wait is needed in
**	order to act on an /AFTER time: the arrival of an appointed time produces no event of itself.
**
**  INPUT PARAMETERS:
**	a_argc	- number of command line arguments.
**	a_argv	- vector of arguments. The following are recognized:
**		    /ROOT=<directory>	subsystem root directory
**		    /JOB_LIMIT=<number>	limit on simultaneously executing jobs
**		    /QUEUE=<name>	default queue name
**		    /FOREGROUND		run in the foreground, diagnostics to the standard error
**		    /VERBOSE		issue messages of informational severity
**		    /LOG_RETAIN=<days>	days a job log left behind is kept, zero disables the sweep
**
**  OUTPUT PARAMETERS:
**	None.
**
**  RETURNS:
**	EXIT_SUCCESS or EXIT_FAILURE.
**--
*/
int	main (
		int	a_argc,
		char **	a_argv
		)
{
struct pollfd	l_pfd [BATCHD$K_MAXCONN + 2];
BATCHD$CONN *	l_conn, *l_next;
long long	l_now, l_sweep;
CLI_CTX *	l_clictx = NULL;
ASC		l_val;
size_t		l_len;
char **		l_argv, *l_nam, *l_end;
cpu_set_t	l_cpus;
char		l_sock [BATCH$K_PATHSZ], l_dir [BATCH$K_PATHSZ], l_buf [64], l_cpustr [BATCH$K_CPUSZ];
char		l_log [BATCH$K_PATHSZ];
int		l_logfd = -1;
int		l_lfd, l_rc, l_dirty, l_cdirty, l_reaped, l_sched, l_fg, l_i, l_n;
int		l_nfd, l_ready, l_sts, l_swept;

	l_fg = 0;

	if ( $IS_FAILURE(msg$init()) )
		return	EXIT_FAILURE;

	/*
	**  The CLI services take the verb from the first element of the vector, whereas argv[0] holds
	**  the specification of the image. A vector of its own is therefore assembled, carrying the
	**  verb in front of the arguments as they were given.
	*/
	if ( !(l_argv = calloc((size_t) a_argc + 1, sizeof(char *))) )
		return	EXIT_FAILURE;

	l_argv[0] = s_verbname;

	for ( l_i = 1; l_i < a_argc; l_i++ )
		l_argv[l_i] = a_argv[l_i];

	if ( $IS_FAILURE(__cli$parse(s_verbs, CLI$M_OPSIGNAL, a_argc, l_argv, (void **) &l_clictx)) )
		{
		util$put("Usage: !AZ [/ROOT=directory] [/JOB_LIMIT=n] [/QUEUE=name] [/FOREGROUND]"
				" [/VERBOSE] [/LOG_RETAIN=days]\n",
				(const fao_prm_t []) { (fao_prm_t) a_argv[0] }, 1);

		return	EXIT_FAILURE;
		}

	/*
	**  An argument that looks like a qualifier but matches nothing is passed over by the CLI
	**  services in silence, so a mistyped qualifier would be ignored instead of rejected. Every
	**  such argument is therefore matched here against the same table, by the same rule: a leading
	**  portion of the name, disregarding case, unambiguous within the table.
	*/
	for ( l_i = 1; l_i < a_argc; l_i++ )
		{
		if ( (a_argv[l_i][0] != '-') && (a_argv[l_i][0] != '/') )
			continue;

		l_nam	= a_argv[l_i] + 1;
		l_end	= strchr(l_nam, '=');
		l_len	= (l_end) ? (size_t) (l_end - l_nam) : strlen(l_nam);
		l_rc	= 0;

		for ( l_n = 0; l_n < BATCHD$K_QMAXNR; l_n++ )
			{
			if ( (l_len) && (l_len <= $ASCLEN(&s_quals[l_n].name))
				&& (!strncasecmp(l_nam, $ASCPTR(&s_quals[l_n].name), l_len)) )
				l_rc++;
			}

		if ( l_rc != 1 )
			{
			$PUTMSG_FAO(BATCH$_IVQUAL, a_argv[l_i]);

			return	EXIT_FAILURE;
			}
		}

	/*
	**  A qualifier that was not given is not an error here, whereas the signalling option would
	**  have CLI$GET_VALUE report every one of them. The option is therefore cleared once the
	**  command line has been parsed.
	*/
	l_clictx->opts &= ~CLI$M_OPSIGNAL;

	if ( $IS_SUCCESS(__cli$get_value(l_clictx, &s_quals[BATCHD$K_QROOT], &l_val)) )
		util$mkfspec(g_root, sizeof(g_root), "!AC", (const fao_prm_t []) { (fao_prm_t) &l_val }, 1);

	if ( $IS_SUCCESS(__cli$get_value(l_clictx, &s_quals[BATCHD$K_QQUEUE], &l_val)) )
		util$mkfspec(g_queue, sizeof(g_queue), "!AC", (const fao_prm_t []) { (fao_prm_t) &l_val }, 1);

	if ( $IS_SUCCESS(__cli$get_value(l_clictx, &s_quals[BATCHD$K_QJOBLIM], &l_val)) )
		g_joblim = atoi($ASCPTR(&l_val));

	/* A qualifier that bears no value is tested for presence alone */
	if ( $IS_SUCCESS(__cli$get_value(l_clictx, &s_quals[BATCHD$K_QFOREGND], NULL)) )
		l_fg = 1;

	if ( $IS_SUCCESS(__cli$get_value(l_clictx, &s_quals[BATCHD$K_QVERBOSE], NULL)) )
		g_verbose = 1;

	if ( $IS_SUCCESS(__cli$get_value(l_clictx, &s_quals[BATCHD$K_QLOGKEEP], &l_val)) )
		g_logkeep = atoi($ASCPTR(&l_val));

	if ( (g_logkeep < 0) || (g_logkeep > 3650) )
		g_logkeep = BATCHD$K_LOGKEEP;

	/*
	**  The pool of processors. The list is verified here, at the start, rather than when the first
	**  job comes to be run: a controller told to spread its jobs over processors that do not exist
	**  is not to discover it an hour later.
	*/
	if ( $IS_SUCCESS(__cli$get_value(l_clictx, &s_quals[BATCHD$K_QLCORE], &l_val)) )
		{
		cpu_set_t	l_pool;
		int		l_pcnt = 0, l_ncpu;

		__util$asc2str(&l_val, l_cpustr);

		/*
		**  The list is accepted both as the parenthesised set DCL writes - (0,3,4) - and as the
		**  bare series taskset(1) takes. The parentheses and the blanks within them are stripped
		**  here, the two spellings meaning the same thing and the difference being one of habit.
		*/
		for ( char *l_r = l_cpustr, *l_w = l_cpustr; ; l_r++ )
			{
			if ( (*l_r != '(') && (*l_r != ')') && (*l_r != ' ') && (*l_r != '\t') )
				*l_w++ = *l_r;

			if ( !*l_r )
				break;
			}

		if ( $IS_FAILURE(util$parse_cpulist(l_cpustr, &l_pool, &l_pcnt)) )
			{
			$PUTMSG_FAO(BATCH$_IVLCORE, l_cpustr);

			return	EXIT_FAILURE;
			}

		/*
		**  A processor named must exist. The set can hold a number for any processor the kernel
		**  could ever have, so a list is otherwise accepted whatever it names, and the mistake
		**  shows itself later as a job that will not start - with nothing to say why.
		*/
		l_ncpu = (int) sysconf(_SC_NPROCESSORS_CONF);

		for ( int i = 0; i < CPU_SETSIZE; i++ )
			{
			if ( !CPU_ISSET((unsigned) i, &l_pool) )
				continue;

			if ( (l_ncpu > 0) && (i >= l_ncpu) )
				{
				$PUTMSG_FAO(BATCH$_IVLCORE, l_cpustr);

				return	EXIT_FAILURE;
				}

			g_lcore[g_lcorecnt++] = i;
			}

		util$mkfspec(g_lcorestr, sizeof(g_lcorestr), "!AZ",
				(const fao_prm_t []) { (fao_prm_t) l_cpustr }, 1);

		if ( !g_lcorecnt )
			{
			$PUTMSG_FAO(BATCH$_IVLCORE, l_cpustr);

			return	EXIT_FAILURE;
			}
		}

	if ( (g_joblim < 1) || (g_joblim > BATCH$K_MAXJOB) )
		g_joblim = BATCH$K_DEFJOBLIM;

	/*
	**  The queues. The default one always exists and takes the job limit of the controller; those
	**  named by /QUEUE are added beside it, and a name without a limit of its own takes the same one:
	**  /QUEUE=(SYS$BATCH,slow:1,fast:3). One qualifier serves both purposes because /QUEUES beside
	**  /QUEUE would be an abbreviation of it, and the services would have to guess which was meant.
	**  The first name of the list is the default queue. A job asking for a queue the table does not
	**  hold is refused at submission - a job
	**  placed in a queue nobody minds would wait for ever, and would look exactly like a job that
	**  is merely waiting its turn.
	*/
	g_quecnt = 0;

	if ( $IS_SUCCESS(__cli$get_value(l_clictx, &s_quals[BATCHD$K_QQUEUE], &l_val)) )
		{
		BATCHD$QUEUE *	l_que;
		char		l_work [BATCH$K_PATHSZ], *l_p, *l_qnext, *l_colon;

		__util$asc2str(&l_val, l_work);

		for ( char *l_r = l_work, *l_w = l_work; ; l_r++ )
			{
			if ( (*l_r != '(') && (*l_r != ')') && (*l_r != ' ') && (*l_r != '\t') )
				*l_w++ = *l_r;

			if ( !*l_r )
				break;
			}

		for ( l_p = l_work; (l_p) && (*l_p); l_p = l_qnext )
			{
			if ( (l_qnext = strchr(l_p, ',')) )
				*l_qnext++ = '\0';

			if ( (!*l_p) || (g_quecnt >= BATCH$K_MAXQUE) )
				continue;

			if ( (l_colon = strchr(l_p, ':')) )
				*l_colon++ = '\0';

			if ( $IS_SUCCESS(s_batchd$que_by_name(l_p, &l_que)) )
				{
				if ( l_colon )
					l_que->qe_joblim = atoi(l_colon);

				continue;
				}

			util$mkfspec(g_queues[g_quecnt].qe_name, sizeof(g_queues[0].qe_name), "!AZ",
					(const fao_prm_t []) { (fao_prm_t) l_p }, 1);

			g_queues[g_quecnt].qe_joblim = (l_colon) ? atoi(l_colon) : g_joblim;

			if ( (g_queues[g_quecnt].qe_joblim < 1) || (g_queues[g_quecnt].qe_joblim > BATCH$K_MAXJOB) )
				g_queues[g_quecnt].qe_joblim = g_joblim;

			g_quecnt++;
			}
		}

	/*
	**  The queue built into the subsystem is always in the table, named or not. Without this a
	**  controller told /QUEUE=slow would have no SYS$BATCH at all, and every entry left in it by a
	**  previous run would wait in a queue that no longer exists - the very thing the table was
	**  introduced to make impossible.
	*/
	if ( !g_quecnt )
		s_batchd$que_add(g_queue, g_joblim, 0);

	s_batchd$que_add(BATCH$K_DEFQUEUE, g_joblim, 0);

	/* The first of the list is the default: a job that names no queue goes there */
	util$mkfspec(g_queue, sizeof(g_queue), "!AZ",
			(const fao_prm_t []) { (fao_prm_t) g_queues[0].qe_name }, 1);

	__cli$cleanup(l_clictx);
	free(l_argv);




	if ( $IS_FAILURE(util$mkpath(g_root, 0755)) )
		return	EXIT_FAILURE;

	if ( $IS_FAILURE(util$mkfspec(l_dir, sizeof(l_dir), "!AZ/!AZ",
				(const fao_prm_t []) { (fao_prm_t) g_root, (fao_prm_t) BATCHD$K_LOGDIR }, 2)) )

		return	EXIT_FAILURE;

	if ( $IS_FAILURE(util$mkpath(l_dir, 0755)) )
		return	EXIT_FAILURE;

	/*
	**  The log the controller keeps of itself is opened here, before anything is issued, so that
	**  it holds the whole of a run - the identification, the loading of the journal and whatever
	**  went wrong with it - and not merely the part that follows the detaching.
	**
	**  UTIL$SETLOGFD points the logging at that file without seizing the standard streams, which
	**  UTIL$DEFLOG would: the streams are still those of whoever started the controller, and are
	**  wanted for the summary issued below.
	*/
	if ( $IS_FAILURE(util$mkfspec(l_log, sizeof(l_log), "!AZ/!AZ",
				(const fao_prm_t []) { (fao_prm_t) g_root, (fao_prm_t) BATCHD$K_LOGNAME }, 2)) )
		return	EXIT_FAILURE;

	if ( !l_fg )
		{
		if ( 0 > (l_logfd = open(l_log, O_WRONLY | O_CREAT | O_APPEND, 0640)) )
			return	$PUTMSG_FAO(BATCH$_OPENOUT, l_log, -1, errno, strerror(errno));

		__util$setlogfd(l_logfd);
		}

	s_batchd$journal_load();

	/*
	**  The self-pipe: the signal handler writes a byte into it and the main loop waits on it
	**  alongside the connections. Both ends are made non-blocking, so that a full pipe cannot hang
	**  the handler and an empty one cannot hang the main loop.
	*/
	if ( 0 > (l_rc = pipe(g_chldpipe)) )
		{
		$PUTMSG_FAO(BATCH$_PIPEERR, l_rc, errno, strerror(errno));

		return	EXIT_FAILURE;
		}

	fcntl(g_chldpipe[0], F_SETFL, O_NONBLOCK);
	fcntl(g_chldpipe[1], F_SETFL, O_NONBLOCK);

	/* A write to a socket the client has closed must not bring the daemon down */
	if ( (0 > s_batchd$sigaction(SIGPIPE, SIG_IGN))
		|| (0 > s_batchd$sigaction(SIGCHLD, s_batchd$sighandler))
		|| (0 > s_batchd$sigaction(SIGTERM, s_batchd$sighandler))
		|| (0 > s_batchd$sigaction(SIGINT,  s_batchd$sighandler)) )
		{
		$PUTMSG_FAO(BATCH$_DAEMONERR, -1, errno, strerror(errno));

		return	EXIT_FAILURE;
		}

	if ( $IS_FAILURE(s_batchd$listen(l_sock, sizeof(l_sock), &l_lfd)) )
		{
		/*
		**  The reason went to the log, which by now is where the diagnostics go. The terminal is
		**  told that the attempt failed and where to read about it, rather than being left to
		**  conclude from a silent return that all is well.
		*/
		if ( !l_fg )
			s_batchd$announce(l_log, 0);

		return	EXIT_FAILURE;
		}

	/*
	**  Detaching is left until everything that can fail has succeeded: the root directory made,
	**  the journal read, the socket bound and listening. A controller that cannot start says so
	**  on the terminal of whoever started it and exits with a failure status, rather than
	**  detaching first and confiding the reason to a log the reader does not yet know about.
	**
	**  The line stating that it started is issued before detaching for the same reason, so that
	**  the command answers rather than returning in silence.
	*/
	$PUTMSG_FAO(BATCH$_IDENT, BATCH$K_FACILITY, BATCH$K_VERSION, BATCH$K_IDENT, BATCH$K_BUILDID);
	g_stime = (long long) time(NULL);

	$PUTMSG_FAO(BATCH$_CTLSTARTED, g_root, g_queue, g_joblim, l_sock);

	/* What was set aside for the batch load, where anything was */
	if ( g_lcorecnt )
		$PUTMSG_FAO(BATCH$_LCOREPOOL, l_cpustr, g_lcorecnt);

	if ( !l_fg )
		{
		/*
		**  The terminal is about to go, so a word is said on it: the diagnostics have been going
		**  to the log all along, and whoever started the controller is owed the outcome and the
		**  name of the file to look in for the rest.
		*/
		s_batchd$announce(l_log, 1);

		if ( 0 > (l_rc = daemon(1, 0)) )
			{
			$PUTMSG_FAO(BATCH$_DAEMONERR, l_rc, errno, strerror(errno));

			return	EXIT_FAILURE;
			}

		/* The streams are gone with the terminal; the log takes them over from here on */
		__util$deflog(l_log, NULL);
		}

	/*
	**  The mask the controller runs under is stated once, at the start. A job inherits it unless
	**  it names one of its own, so a controller started under taskset constrains everything it
	**  creates - and the reader is now told of it rather than left to find out.
	*/
	if ( !sched_getaffinity(0, sizeof(l_cpus), &l_cpus) )
		{
		util$fmt_cpulist(&l_cpus, l_cpustr, sizeof(l_cpustr));
		$PUTMSG_FAO(BATCH$_INFO, l_cpustr);
		}

	s_batchd$log_sweep(&l_swept);
	l_sweep = (long long) time(NULL) + BATCHD$K_SWEEPSEC;

	while ( !g_shutdown )
		{
		s_batchd$reap(&l_reaped);
		s_batchd$schedule(&l_sched);

		if ( l_reaped + l_sched )
			s_batchd$journal_save();

		/* Keep the log from growing without bound */
		__util$rewindlogfile(BATCHD$K_LOGMAX);

		/* The log directory is swept now and then, not on every turn of the loop */
		if ( (long long) time(NULL) >= l_sweep )
			{
			s_batchd$log_sweep(&l_swept);
			l_sweep = (long long) time(NULL) + BATCHD$K_SWEEPSEC;
			}

		/*
		**  The set watched comprises the listening socket, the self-pipe and every connection open
		**  at this moment, each awaited for what its state calls for. No descriptor is ever waited
		**  upon on its own, so a client that stops mid-request holds up nothing but itself.
		*/
		l_pfd[0].fd	= l_lfd;
		l_pfd[0].events	= POLLIN;
		l_pfd[1].fd	= g_chldpipe[0];
		l_pfd[1].events	= POLLIN;
		l_nfd		= 2;

		for ( l_conn = $CONNQUE_FIRST(); l_conn; l_conn = $CONNQUE_NEXT(l_conn) )
			{
			l_pfd[l_nfd].fd		= l_conn->cn_fd;
			l_pfd[l_nfd].events	= (l_conn->cn_state == CONN$K_WRRSP) ? POLLOUT : POLLIN;
			l_nfd++;
			}

		l_rc = poll(l_pfd, (nfds_t) l_nfd, BATCHD$K_POLLMS);

		if ( (0 > l_rc) && (errno != EINTR) )
			{
			$PUTMSG_FAO(BATCH$_POLLERR, l_rc, errno, strerror(errno));
			continue;
			}

		l_now	= (long long) time(NULL);
		l_dirty	= 0;

		/* Drain the self-pipe: the accumulated bytes carry no meaning of their own */
		if ( (l_rc > 0) && (l_pfd[1].revents & POLLIN) )
			{
			while ( read(g_chldpipe[0], l_buf, sizeof(l_buf)) > 0 )
				;
			}

		/*
		**  The connections are visited before the listening socket, so that a newly accepted one
		**  is not acted upon in the same turn as it is created: its place in the watched set is
		**  established on the next.
		**
		**  The successor is taken before the step, since the step may release the entry.
		*/
		l_nfd = 2;

		for ( l_conn = $CONNQUE_FIRST(); l_conn; l_conn = l_next )
			{
			l_next		= $CONNQUE_NEXT(l_conn);
			l_ready		= (l_rc > 0) ? l_pfd[l_nfd].revents : 0;
			l_cdirty	= 0;
			l_nfd++;

			if ( l_ready & (POLLERR | POLLHUP | POLLNVAL) )
				{
				s_batchd$conn_close(l_conn);
				continue;
				}

			if ( l_ready & (POLLIN | POLLOUT) )
				{
				/*
				**  The state is examined before the step, not after: a step that reads a
				**  request may act upon it and leave the connection writing its reply, and
				**  the indication that the queue changed would then be discarded.
				*/
				if ( l_conn->cn_state == CONN$K_WRRSP )
					l_sts = s_batchd$conn_write(l_conn);
				else	l_sts = s_batchd$conn_read(l_conn, &l_cdirty);

				l_dirty += l_cdirty;

				if ( $IS_FAILURE(l_sts) || $MATCH_COND(l_sts, BATCH$_ENDOFDATA) )
					{
					s_batchd$conn_close(l_conn);
					continue;
					}
				}

			/*
			**  A connection that has made no progress within the period allowed is dropped: a
			**  descriptor is not to be held by a client that has forgotten about it.
			*/
			if ( l_conn->cn_deadline <= l_now )
				{
				$PUTMSG_FAO(BATCH$_CONNCLOSED, l_conn->cn_uid);
				s_batchd$conn_close(l_conn);
				}
			}

		if ( (l_rc > 0) && (l_pfd[0].revents & POLLIN) )
			s_batchd$conn_accept(l_lfd);

		/* A job just queued is to start at once, without awaiting the next turn of the loop */
		if ( l_dirty )
			{
			s_batchd$schedule(&l_sched);
			s_batchd$journal_save();
			}
		}

	/* Whatever is still connected is let go before the socket itself */
	for ( l_conn = $CONNQUE_FIRST(); l_conn; l_conn = l_next )
		{
		l_next = $CONNQUE_NEXT(l_conn);
		s_batchd$conn_close(l_conn);
		}

	$PUTMSG_FAO(BATCH$_CTLSTOPPING);

	s_batchd$journal_save();
	close(l_lfd);
	unlink(l_sock);

	return	EXIT_SUCCESS;
}
