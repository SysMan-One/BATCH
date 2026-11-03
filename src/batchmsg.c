/*
**++
**  MODULE:		BATCHMSG.C
**  IDENT:		X-02-02ECO02
**  REV:		02.02.02
**
**  FACILITY:		BATCH
**
**  ABSTRACT:		Message table of the BATCH facility. The table is handed to UTILITY$ROUTINES by
**			MSG$INIT and is thereafter consulted by the library itself: $PUTMSG locates the
**			record for a condition value and formats the message from the text held here.
**
**			A message text is an FAO control string - !AZ, !SL, !UL, !SQ - since $PUTMSG_FAO
**			renders it with the FAO engine. It carries neither the envelope %BATCH-s-IDENT nor
**			the time: the library supplies both. A printf(3) conversion is not recognized and
**			is printed as it stands.
**
**			Where one condition serves several places that need to say different things, the
**			distinguishing part is a parameter of the text rather than a condition of its
**			own: the number of conditions stays as it is and $MATCH_COND keeps working.
**
**			The table is not declared const: __UTIL$INIMSG sorts it in place.
**
**  AUTHOR:		StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
**
**  CREATION DATE:	28-JUL-2026
**
**  MODIFICATION HISTORY:
**
**	28-JUL-2026	RRL	X-01-01	Initial revision.
**
**	28-JUL-2026	RRL	X-01-02	Comments and message texts converted to English.
**
**	28-JUL-2026	RRL	X-01-03	Converted to the message facility of UTILITY$ROUTINES: the table is now
**				an array of EMSG_RECORD registered through __UTIL$INIMSG, and the routines
**				MSG$GET_TEXT and MSG$SEV_NAME were withdrawn together with the local
**				lookup they performed.
**
**	28-JUL-2026	RRL	X-01-04	Texts of the conditions raised by a failing C RTL routine were given the
**				shape <call>()-><result>, errno: <number> (<text>).
**
**	29-JUL-2026	RRL	X-01-05	Text of BATCH$_JOBREQUEUED added.
**
**	29-JUL-2026	RRL	X-01-06	Text of BATCH$_LOGSWEPT added.
**
**	29-JUL-2026	RRL	X-01-07	Texts of BATCH$_IVCPU and BATCH$_AFFERR added.
**
**	 7-AUG-2026	RRL	X-01-08	The facility name is given to the message descriptor. UTILITY$ROUTINES
**				V.01-24 restored the FACNAME field of EMSG_RECORD_DESC, and a descriptor
**				that leaves it unset hands the library a null where it expects a name.
**
**	20-AUG-2026	RRL	X-01-09	Texts for BATCH$_PURGED and BATCH$_NOTCONF added.
**
**	28-AUG-2026	RRL	X-01-10	The table is written for the FAO engine: !AZ, !SL, !UL and !SQ in
**				place of the printf conversions, and the envelope %FAC-S-IDENT is no longer
**				in the text. UTILITY$ROUTINES V.01-24 restored the FACNAME field of the
**				descriptor and frames a message itself; a text that carried the envelope of
**				its own was framed twice.
**
**	31-AUG-2026	RRL	X-01-11	$MSGREC names the trailing FACNAME of the record and leaves it NULL:
**				UTILITY$ROUTINES V.01-28 stamps it at registration so that a record answers
**				for its own envelope without a second walk of the descriptor list.
**
**	 8-SEP-2026	RRL	X-01-12	Texts of BATCH$_ENTMODIFIED, BATCH$_NOTMODIFIABLE and BATCH$_NOTHINGSET
**				added for the SET ENTRY command.
**
**	 8-OCT-2026	RRL	X-01-13	Text of BATCH$_NOJOBS follows the selection of SHOW without /ALL, which
**				takes in the jobs that wait (BATCHD.C X-01-32).
**
**	18-OCT-2026	RRL	X-01-13ECO01	Text of BATCH$_IVENTRY carried a printf conversion that X-01-10 had
**				missed, and the FAO engine printed it as it stood: "%s" does not name a queue
**				entry. It is !AZ now.
**
**	18-OCT-2026	RRL	X-02-02	Revision raised to X-02-02 together with the product; no change of its own.
**
**	25-OCT-2026	RRL	X-02-02ECO01	Text of BATCH$_NOPRIVQUE (BATCHMSG.H X-02-02ECO01).
**
**	28-OCT-2026	CLD	X-02-02ECO02	Text of BATCH$_SCRIPTDEV (BATCHMSG.H X-02-02ECO02).
**
**	 3-NOV-2026	RRL	X-02-02ECO03	$MSGREC names the MNEMONIC field of EMSG_RECORD, which UTILITY$ROUTINES
**				has gained, and leaves it NULL: the 100 records of the table drew a
**				-Wmissing-field-initializers remark each. No change in what a message says.
**--
*/

#include	<stddef.h>

#include	"batchdef.h"

#undef	__MODULE__
#undef	__IDENT__
#undef	__REV__

#define	__MODULE__	"BATCHMSG"
#define	__IDENT__	"X-02-02ECO03"
#define	__REV__		"02.02.03"

/*
**  A single row of the message table. The text is held in an ASCIC container, so its length is
**  taken from the literal at translation time.
*/
/*
**  The trailing FACNAME and MNEMONIC are initialised to NULL and left so: UTILITY$ROUTINES V.01-28
**  stamps FACNAME into every record at registration, from the name in the descriptor, and a record
**  answers for its own envelope thereafter; a NULL MNEMONIC leaves the envelope as it was - the text
**  of every message here begins with its own mnemonic. Both are written here all the same - an
**  initialiser that names every field of a structure says it means to, and the compiler is spared
**  a remark about the one it does not.
*/
#define	$MSGREC(cond, txt)	{ (int) (cond), { (unsigned char) (sizeof(txt) - 1), txt }, NULL, NULL }

static	EMSG_RECORD	s_msgtab [] =
	{
	$MSGREC(BATCH$_NORMAL,		"NORMAL, normal successful completion"					),
	$MSGREC(BATCH$_INFO,		"INFO, the controller runs on processors !AZ, and a job inherits that"		),
	$MSGREC(BATCH$_BUGCHECK,	"BUGCHECK, internal inconsistency: !AZ"					),
	$MSGREC(BATCH$_BUFOVF,		"BUFOVF, result of !SL bytes does not fit in !UL"				),
	$MSGREC(BATCH$_INVTIME,		"INVTIME, unparsable time specification /AFTER=!AZ"				),
	$MSGREC(BATCH$_MKDIRERR,	"MKDIRERR, cannot create !AZ, mkdir()->!SL, errno: !SL (!AZ)"			),
	$MSGREC(BATCH$_IOERR,		"IOERR, error transferring data, !AZ()->!SL, errno: !SL (!AZ)"			),
	$MSGREC(BATCH$_ENDOFDATA,	"ENDOFDATA, data ended earlier than expected, !AZ()->!SL after !UL of !UL bytes"),
	$MSGREC(BATCH$_INVSTATE,	"INVSTATE, unknown job state"							),
	$MSGREC(BATCH$_CWDERR,		"CWDERR, getcwd()->NULL, errno: !SL (!AZ)"					),
	$MSGREC(BATCH$_SMALLBUF,	"SMALLBUF, supplied buffer is manifestly too small"				),
	$MSGREC(BATCH$_JOBQUED,		"JOBQUED, job !UL (!AZ) queued on !AZ, state !AZ"					),
	$MSGREC(BATCH$_JOBSTART,	"JOBSTART, job !UL (!AZ) started, pid=!SL, log !AZ"				),
	$MSGREC(BATCH$_JOBDONE,		"JOBDONE, job !UL (!AZ) completed successfully in !SQ s"			),
	$MSGREC(BATCH$_JOBFAIL,		"JOBFAIL, job !UL (!AZ) terminated with status !SL!AZ, log !AZ"			),
	$MSGREC(BATCH$_JOBABORT,	"JOBABORT, job !UL (!AZ) terminated by signal !SL, log !AZ"			),
	$MSGREC(BATCH$_JOBORPHAN,	"JOBORPHAN, job !UL was orphaned by a restart of the job controller"		),
	$MSGREC(BATCH$_NOSLOT,		"NOSLOT, the job table is full, capacity !SL entries, while !AZ"		),
	$MSGREC(BATCH$_NOSUCHENT,	"NOSUCHENT, no queue entry !UL"						),
	$MSGREC(BATCH$_NOPRIV,		"NOPRIV, entry !UL belongs to uid=!SL, access denied"				),
	$MSGREC(BATCH$_NOTHELD,		"NOTHELD, entry !UL is not in the holding state"				),
	$MSGREC(BATCH$_ENTDELETED,	"ENTDELETED, entry !UL deleted from the queue"					),
	$MSGREC(BATCH$_ENTRELEASED,	"ENTRELEASED, entry !UL released from the holding state"			),
	$MSGREC(BATCH$_ENTMODIFIED,	"ENTMODIFIED, entry !UL modified: !AZ"						),
	$MSGREC(BATCH$_ABORTREQ,	"ABORTREQ, SIGTERM sent to job !UL (pid=!SL)"					),
	$MSGREC(BATCH$_JOBREQUEUED,	"JOBREQUEUED, job !UL (!AZ) requeued for restart, attempt !UL"			),
	$MSGREC(BATCH$_OPENIN,		"OPENIN, error opening !AZ for input, open()->!SL, errno: !SL (!AZ)"		),
	$MSGREC(BATCH$_OPENOUT,		"OPENOUT, error opening !AZ for output, open()->!SL, errno: !SL (!AZ)"		),
	$MSGREC(BATCH$_WRITEERR,	"WRITEERR, error writing !AZ, errno: !SL (!AZ)"					),
	$MSGREC(BATCH$_RENAMEERR,	"RENAMEERR, cannot rename !AZ to !AZ, rename()->!SL, errno: !SL (!AZ)"		),
	$MSGREC(BATCH$_NOJOURNAL,	"NOJOURNAL, journal !AZ does not exist, the queue is empty"			),
	$MSGREC(BATCH$_JRNCORRUPT,	"JRNCORRUPT, journal !AZ is corrupt: the header is missing"			),
	$MSGREC(BATCH$_JRNVERSION,	"JRNVERSION, journal !AZ is incompatible: magic=!8XL format=!UL"		),
	$MSGREC(BATCH$_JRNTRUNC,	"JRNTRUNC, journal !AZ ends at record !UL of !UL"				),
	$MSGREC(BATCH$_JRNLOADED,	"JRNLOADED, records loaded from the journal: !UL"				),
	$MSGREC(BATCH$_NOSCRIPT,	"NOSCRIPT, script !AZ: !AZ, !AZ()->!SL, errno: !SL (!AZ)"				),
	$MSGREC(BATCH$_LOGSPEC,		"LOGSPEC, the job log specification cannot be constructed"			),
	$MSGREC(BATCH$_NODIR,		"NODIR, directory !AZ is not accessible, realpath()->NULL, errno: !SL (!AZ)"	),
	$MSGREC(BATCH$_SCRIPTCHG,	"SCRIPTCHG, entry !UL: script !AZ was replaced since it was submitted"		),
	$MSGREC(BATCH$_SCRIPTDEV,	"SCRIPTDEV, entry !UL: script !AZ is on the same file system under device !UL:!UL, was !UL:!UL"	),
	$MSGREC(BATCH$_LOGSWEPT,	"LOGSWEPT, !SL job log!AZ removed from !AZ, older than !SL day!AZ"			),
	$MSGREC(BATCH$_NOCONNECT,	"NOCONNECT, job controller is not available, connect(!AZ)->!SL, errno: !SL (!AZ)"	),
	$MSGREC(BATCH$_SOCKERR,		"SOCKERR, socket(AF_UNIX)->!SL, errno: !SL (!AZ)"				),
	$MSGREC(BATCH$_BINDERR,		"BINDERR, bind(!AZ)->!SL, errno: !SL (!AZ)"					),
	$MSGREC(BATCH$_LISTENERR,	"LISTENERR, listen(!AZ)->!SL, errno: !SL (!AZ)"					),
	$MSGREC(BATCH$_ACCEPTERR,	"ACCEPTERR, accept()->!SL, errno: !SL (!AZ)"					),
	$MSGREC(BATCH$_PEERCRED,	"PEERCRED, getsockopt(SO_PEERCRED)->!SL, errno: !SL (!AZ)"			),
	$MSGREC(BATCH$_BADMAGIC,	"BADMAGIC, invalid frame signature: !8XL"					),
	$MSGREC(BATCH$_BADLENGTH,	"BADLENGTH, invalid !AZ payload length: !UL"					),
	$MSGREC(BATCH$_BADFUNC,		"BADFUNC, unknown function code !UL"						),
	$MSGREC(BATCH$_NOREPLY,		"NOREPLY, no reply to the !AZ request"						),
	$MSGREC(BATCH$_SENDERR,		"SENDERR, error transmitting !AZ"						),
	$MSGREC(BATCH$_RECVERR,		"RECVERR, error receiving !AZ"							),
	$MSGREC(BATCH$_INUSE,		"INUSE, another job controller is already serving !AZ"				),
	$MSGREC(BATCH$_SOCKPATH,	"SOCKPATH, socket specification !AZ exceeds !UL bytes"				),
	$MSGREC(BATCH$_CONNCLOSED,	"CONNCLOSED, client uid=!SL closed the connection before sending a header"	),
	$MSGREC(BATCH$_IVQUAL,		"IVQUAL, unrecognized or ambiguous qualifier !AZ"				),
	$MSGREC(BATCH$_MISSVAL,		"MISSVAL, qualifier /!AZ requires a value"					),
	$MSGREC(BATCH$_IVQVAL,		"IVQVAL, invalid value /RETAIN=!AZ"						),
	$MSGREC(BATCH$_IVPRIO,		"IVPRIO, the value of /PRIORITY is outside 1..!SL"				),
	$MSGREC(BATCH$_MAXPARM,		"MAXPARM, more than !SL parameters, the excess were discarded"			),
	$MSGREC(BATCH$_NOPARAM,		"NOPARAM, no script specification was given"					),
	$MSGREC(BATCH$_EXTRAPARM,	"EXTRAPARM, extraneous command parameter: !AZ"					),
	$MSGREC(BATCH$_NOSUCHTOPIC,	"NOSUCHTOPIC, no help on !AZ"							),
	$MSGREC(BATCH$_IVENTRY,		"IVENTRY, \"!AZ\" does not name a queue entry"					),
	$MSGREC(BATCH$_PURGED,		"PURGED, !UL entr!AZ removed from the queue"					),
	$MSGREC(BATCH$_NOTCONF,		"NOTCONF, not confirmed, nothing was done"					),
	$MSGREC(BATCH$_NOTMODIFIABLE,	"NOTMODIFIABLE, entry !UL is !AZ, the attribute cannot be altered now"		),
	$MSGREC(BATCH$_NOTHINGSET,	"NOTHINGSET, no attribute to set was given"					),
	$MSGREC(BATCH$_QUEORPHAN,	"QUEORPHAN, queue !AZ was not configured; restored paused for its entries"	),
	$MSGREC(BATCH$_NOSUCHQUE,	"NOSUCHQUE, there is no queue named !AZ"					),
	$MSGREC(BATCH$_QUEPAUSED,	"QUEPAUSED, queue !AZ is paused"						),
	$MSGREC(BATCH$_QUERESUMED,	"QUERESUMED, queue !AZ is resumed"						),
	$MSGREC(BATCH$_NOJOBS,		"NOJOBS, no job executing, waiting or failed in the queue"			),
	$MSGREC(BATCH$_EMPTYQUE,	"EMPTYQUE, the queue holds no jobs"						),
	$MSGREC(BATCH$_SYNCDONE,	"SYNCDONE, job !UL completed"							),
	$MSGREC(BATCH$_SYNCFAIL,	"SYNCFAIL, job !UL ended with !AZ"						),
	$MSGREC(BATCH$_SYNCTMO,		"SYNCTMO, job !UL has not ended within !UL second!AZ"				),
	$MSGREC(BATCH$_QUEUED,		"QUEUED, job !AZ queued on !AZ as entry !UL"					),
	$MSGREC(BATCH$_QUEUEDHELD,	"QUEUEDHELD, job !AZ queued on !AZ as entry !UL, holding"			),
	$MSGREC(BATCH$_QUEUEDUNTIL,	"QUEUEDUNTIL, job !AZ queued on !AZ as entry !UL, holding until !AZ"		),
	$MSGREC(BATCH$_RUNASUSER,	"RUNASUSER, job !UL submitted by uid !SL to run as !AZ (uid !SL, gid !SL)"		),
	$MSGREC(BATCH$_NOPRIVUSER,	"NOPRIVUSER, uid !SL may not submit a job to run as !AZ"			),
	$MSGREC(BATCH$_NOPRIVQUE,	"NOPRIVQUE, uid !SL may not pause or resume queue !AZ"			),
	$MSGREC(BATCH$_NOSUCHUSER,	"NOSUCHUSER, there is no user !AZ on this system"				),
	$MSGREC(BATCH$_IVCPU,		"IVCPU, unparsable processor list /CPU=!AZ"					),
	$MSGREC(BATCH$_IVVERB,		"IVVERB, unrecognized command"						),
	$MSGREC(BATCH$_NOENTRY,		"NOENTRY, no queue entry number was given"					),
	$MSGREC(BATCH$_SUBREJECT,	"SUBREJECT, the controller rejected the request: !AZ (!8XL)"			),
	$MSGREC(BATCH$_FORKERR,		"FORKERR, job !UL, fork()->!SL, errno: !SL (!AZ)"					),
	$MSGREC(BATCH$_DAEMONERR,	"DAEMONERR, daemon()->!SL, errno: !SL (!AZ)"					),
	$MSGREC(BATCH$_PIPEERR,		"PIPEERR, pipe()->!SL, errno: !SL (!AZ)"						),
	$MSGREC(BATCH$_POLLERR,		"POLLERR, poll()->!SL, errno: !SL (!AZ)"						),
	$MSGREC(BATCH$_AFFERR,		"AFFERR, job !UL: sched_setaffinity()->!SL, errno: !SL (!AZ)"			),
	$MSGREC(BATCH$_UNKPROC,		"UNKPROC, an unknown process terminated, pid=!SL"				),
	$MSGREC(BATCH$_IVLCORE,		"IVLCORE, /LCORE=!AZ names a processor this system has not"			),
	$MSGREC(BATCH$_LCOREPOOL,	"LCOREPOOL, jobs are spread over processors !AZ, !UL in the pool"		),
	$MSGREC(BATCH$_CTLSTARTED,	"CTLSTARTED, job controller started, root=!AZ, queue=!AZ, job limit=!SL, socket=!AZ"),
	$MSGREC(BATCH$_CTLSTOPPING,	"CTLSTOPPING, job controller is stopping"					),
	$MSGREC(BATCH$_IDENT,		"IDENT, !AZ !AZ (!AZ), build !AZ"							),
	};

static	EMSG_RECORD_DESC	s_msgdsc =
	{
	NULL,
	sizeof(s_msgtab) / sizeof(s_msgtab[0]),
	BATCH$K_FACNO,
	s_msgtab,
	BATCH$K_FACILITY				/* The name the library frames a message with	*/
	};

/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Registers the message table of the facility with UTILITY$ROUTINES. It is to be called once,
**	at the start of an image, before any message is issued: until it has been called, $PUTMSG
**	finds no record and falls back on a bare rendering of the condition value.
**
**	The library sorts the table in place, so the routine is not reentrant and the table is not
**	declared const.
**
**  INPUT PARAMETERS:
**	None.
**
**  OUTPUT PARAMETERS:
**	None.
**
**  RETURNS:
**	BATCH$_NORMAL or BATCH$_BUGCHECK.
**--
*/
int	msg$init (void)
{
	if ( $IS_FAILURE(__util$inimsg(&s_msgdsc)) )
		return	BATCH$_BUGCHECK;

	return	BATCH$_NORMAL;
}
