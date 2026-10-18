/*
**++
**  MODULE:		BATCHMSG.H
**  IDENT:		X-02-02
**  REV:		02.02.00
**
**  FACILITY:		BATCH
**
**  ABSTRACT:		Condition values of the BATCH facility. This module is the functional equivalent
**			of the output of the OpenVMS MESSAGE utility: it defines the facility number and
**			one condition value for every distinguishable outcome, while the companion module
**			BATCHMSG.C carries the table of mnemonics and default message texts.
**
**			Every routine of the facility returns a specific condition value rather than an
**			indication of success, so that a caller can tell one cause of failure from
**			another with the $MATCH_COND macro without examining message text. To test
**			merely whether the operation succeeded, $IS_SUCCESS suffices, because the
**			low-order bit of a condition value is the success indicator.
**
**			Message numbers are distributed over areas with gaps between them, so that
**			adding a condition to an existing area does not displace numbers already
**			assigned:
**
**			    1 ..   9	general
**			   10 ..  29	general-purpose routines, module UTIL
**			   30 ..  49	queue and jobs
**			   50 ..  69	files and the queue journal
**			   70 ..  89	communication between the utility and the job controller
**			   90 .. 109	command line parsing
**			  110 .. 129	process creation and job controller operation
**
**  AUTHOR:		StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
**
**  CREATION DATE:	28-JUL-2026
**
**  MODIFICATION HISTORY:
**
**	28-JUL-2026	RRL	X-01-01	Initial revision.
**
**	28-JUL-2026	RRL	X-01-02	Comments converted to English.
**
**	28-JUL-2026	RRL	X-01-03	Module STSDEF.H was withdrawn: the severity codes and the composition
**				of a condition value are taken from UTILITY$ROUTINES, and the outcome
**				tests, which the library does not provide, moved here. The facility-
**				specific indicator is no longer set, since the library treats bits 3..15
**				as the message number in their entirety; the numeric values of the
**				conditions changed accordingly, which is of no consequence as none of
**				them is persisted.
**
**	29-JUL-2026	RRL	X-01-04	Condition BATCH$_JOBREQUEUED added: a job submitted with /RESTART is put
**				back in the queue when the controller is started again.
**
**	29-JUL-2026	RRL	X-01-05	Condition BATCH$_LOGSWEPT added: job logs left behind are swept.
**
**	29-JUL-2026	RRL	X-01-06	Conditions BATCH$_IVCPU and BATCH$_AFFERR added for the /CPU qualifier.
**
**	20-AUG-2026	RRL	X-01-07	Conditions BATCH$_PURGED and BATCH$_NOTCONF added.
**
**	 8-SEP-2026	RRL	X-01-08	Conditions BATCH$_ENTMODIFIED, BATCH$_NOTMODIFIABLE and BATCH$_NOTHINGSET
**				added for the SET ENTRY command.
**
**	18-OCT-2026	RRL	X-02-02	Revision raised to X-02-02 together with the product; no change of its own.
**--
*/

#ifndef	__BATCHMSG_H__
#define	__BATCHMSG_H__	1

#undef	__MODULE__
#undef	__IDENT__
#undef	__REV__

#define	__MODULE__	"BATCHMSG"
#define	__IDENT__	"X-02-02"
#define	__REV__		"02.02.00"

/*
**  The layout of a condition value, the severity codes STS$K_* and the macros $FAC, $MSG, $SEV,
**  $DEFFAC and $DEFMSG are declared by UTILITY$ROUTINES:
**
**	 31                    16 15                     3 2      0
**	+------------------------+------------------------+-------+
**	|     facility number    |     message number     |  sev  |
**	+------------------------+------------------------+-------+
*/
#include	"utility_routines.h"

/*
**  Facility number. Bit 11 of the field, that is bit 27 of the word, marks a facility number not
**  assigned by the system vendor, so this one cannot collide with theirs.
*/
#define	BATCH$K_FACNO		0x0801
#define	BATCH$K_FACNAME		"BATCH"

/*
**  Composition of a condition value of this facility.
*/
#define	$BATCH_COND(msg, sev)	( $DEFFAC(BATCH$K_FACNO) | $DEFMSG(msg) | ((unsigned) (sev) & 0x7) )

/*
**  Outcome tests. UTILITY$ROUTINES does not supply them, although it relies on the same property
**  internally: the low-order bit of a condition value is the success indicator, so the severities
**  "success" and "informational" are odd and the remaining three are even.
*/
#define	$IS_SUCCESS(sts)	((((unsigned) (sts)) & 1U) == 1U)
#define	$IS_FAILURE(sts)	((((unsigned) (sts)) & 1U) == 0U)

/*
**  Comparison of two condition values by their identification, that is, disregarding severity.
**  This tells one condition from another without relying on exact equality.
*/
#define	$MATCH_COND(sts1, sts2)	($MSG(sts1) == $MSG(sts2))

/*
**  General condition values, 1 .. 9
*/
#define	BATCH$_NORMAL		$BATCH_COND(  1, STS$K_SUCCESS)	/* Normal completion 						*/
#define	BATCH$_INFO		$BATCH_COND(  2, STS$K_INFO)	/* Informational message 					*/
#define	BATCH$_BUGCHECK		$BATCH_COND(  3, STS$K_FATAL)	/* Internal inconsistency 					*/

/*
**  General-purpose routines, module UTIL, 10 .. 29
*/
#define	BATCH$_BUFOVF		$BATCH_COND( 10, STS$K_ERROR)	/* Result does not fit the buffer 				*/
#define	BATCH$_INVTIME		$BATCH_COND( 11, STS$K_ERROR)	/* Time specification unparsable 				*/
#define	BATCH$_MKDIRERR		$BATCH_COND( 12, STS$K_ERROR)	/* Directory cannot be created 					*/
#define	BATCH$_IOERR		$BATCH_COND( 13, STS$K_ERROR)	/* Error transferring data 					*/
#define	BATCH$_ENDOFDATA	$BATCH_COND( 14, STS$K_ERROR)	/* Data ended prematurely 					*/
#define	BATCH$_INVSTATE		$BATCH_COND( 15, STS$K_WARN)	/* Unknown job state 						*/
#define	BATCH$_CWDERR		$BATCH_COND( 16, STS$K_ERROR)	/* Current directory unobtainable 				*/
#define	BATCH$_SMALLBUF		$BATCH_COND( 17, STS$K_ERROR)	/* Output buffer manifestly too small 				*/

/*
**  Queue and jobs, 30 .. 49
*/
#define	BATCH$_JOBQUED		$BATCH_COND( 30, STS$K_SUCCESS)	/* Job placed in the queue 					*/
#define	BATCH$_JOBSTART		$BATCH_COND( 31, STS$K_SUCCESS)	/* Job started 							*/
#define	BATCH$_JOBDONE		$BATCH_COND( 32, STS$K_SUCCESS)	/* Job completed successfully 					*/
#define	BATCH$_JOBFAIL		$BATCH_COND( 33, STS$K_ERROR)	/* Job returned a nonzero status 				*/
#define	BATCH$_JOBABORT		$BATCH_COND( 34, STS$K_ERROR)	/* Job terminated by a signal 					*/
#define	BATCH$_JOBORPHAN	$BATCH_COND( 35, STS$K_WARN)	/* Job orphaned by a restart 					*/
#define	BATCH$_NOSLOT		$BATCH_COND( 36, STS$K_ERROR)	/* Job table is full 						*/
#define	BATCH$_NOSUCHENT	$BATCH_COND( 37, STS$K_ERROR)	/* No such queue entry 						*/
#define	BATCH$_NOPRIV		$BATCH_COND( 38, STS$K_ERROR)	/* No privilege for the entry 					*/
#define	BATCH$_NOTHELD		$BATCH_COND( 39, STS$K_ERROR)	/* Entry is not in the holding state 				*/
#define	BATCH$_ENTDELETED	$BATCH_COND( 40, STS$K_SUCCESS)	/* Entry deleted from the queue 				*/
#define	BATCH$_ENTRELEASED	$BATCH_COND( 41, STS$K_SUCCESS)	/* Entry released from holding 					*/
#define	BATCH$_ENTMODIFIED	$BATCH_COND( 49, STS$K_SUCCESS)	/* Attributes of an entry altered 				*/
#define	BATCH$_ABORTREQ		$BATCH_COND( 42, STS$K_SUCCESS)
#define	BATCH$_JOBREQUEUED	$BATCH_COND( 43, STS$K_SUCCESS)	/* Job requeued for restart 					*/

/*
**  Files and the queue journal, 50 .. 69
*/
#define	BATCH$_OPENIN		$BATCH_COND( 50, STS$K_ERROR)	/* Error opening a file for input 				*/
#define	BATCH$_OPENOUT		$BATCH_COND( 51, STS$K_ERROR)	/* Error opening a file for output 				*/
#define	BATCH$_WRITEERR		$BATCH_COND( 52, STS$K_ERROR)	/* Error writing a file 					*/
#define	BATCH$_RENAMEERR	$BATCH_COND( 53, STS$K_ERROR)	/* Error renaming a file 					*/
#define	BATCH$_NOJOURNAL	$BATCH_COND( 54, STS$K_INFO)	/* Queue journal does not exist 				*/
#define	BATCH$_JRNCORRUPT	$BATCH_COND( 55, STS$K_ERROR)	/* Queue journal is corrupt 					*/
#define	BATCH$_JRNVERSION	$BATCH_COND( 56, STS$K_ERROR)	/* Queue journal is incompatible 				*/
#define	BATCH$_JRNTRUNC		$BATCH_COND( 57, STS$K_WARN)	/* Queue journal is truncated 					*/
#define	BATCH$_JRNLOADED	$BATCH_COND( 58, STS$K_INFO)	/* Queue restored from the journal 				*/
#define	BATCH$_NOSCRIPT		$BATCH_COND( 59, STS$K_ERROR)	/* Script is not accessible 					*/
#define	BATCH$_LOGSPEC		$BATCH_COND( 60, STS$K_ERROR)	/* Log specification unbuildable 				*/
#define	BATCH$_NODIR		$BATCH_COND( 61, STS$K_ERROR)	/* Directory is not accessible 					*/
#define	BATCH$_SCRIPTCHG	$BATCH_COND( 63, STS$K_ERROR)	/* Script replaced after submission */
#define	BATCH$_LOGSWEPT		$BATCH_COND( 62, STS$K_INFO)	/* Job logs left behind were swept 				*/

/*
**  Communication between the utility and the job controller, 70 .. 89
*/
#define	BATCH$_NOCONNECT	$BATCH_COND( 70, STS$K_ERROR)	/* Job controller is not available 				*/
#define	BATCH$_SOCKERR		$BATCH_COND( 71, STS$K_FATAL)	/* Error creating a socket 					*/
#define	BATCH$_BINDERR		$BATCH_COND( 72, STS$K_FATAL)	/* Error binding a socket 					*/
#define	BATCH$_LISTENERR	$BATCH_COND( 73, STS$K_FATAL)	/* Error listening on a socket 					*/
#define	BATCH$_ACCEPTERR	$BATCH_COND( 74, STS$K_ERROR)	/* Error accepting a connection 				*/
#define	BATCH$_PEERCRED		$BATCH_COND( 75, STS$K_ERROR)	/* Sender identity unobtainable 				*/
#define	BATCH$_BADMAGIC		$BATCH_COND( 76, STS$K_ERROR)	/* Invalid frame signature 					*/
#define	BATCH$_BADLENGTH	$BATCH_COND( 77, STS$K_ERROR)	/* Invalid payload length 					*/
#define	BATCH$_BADFUNC		$BATCH_COND( 78, STS$K_ERROR)	/* Unknown function code 					*/
#define	BATCH$_NOREPLY		$BATCH_COND( 79, STS$K_ERROR)	/* No reply from the job controller 				*/
#define	BATCH$_SENDERR		$BATCH_COND( 80, STS$K_ERROR)	/* Error transmitting a request 				*/
#define	BATCH$_RECVERR		$BATCH_COND( 81, STS$K_ERROR)	/* Error receiving a reply 					*/
#define	BATCH$_INUSE		$BATCH_COND( 88, STS$K_FATAL)	/* Another controller owns the root */
#define	BATCH$_SOCKPATH		$BATCH_COND( 82, STS$K_FATAL)	/* Socket specification too long 				*/
#define	BATCH$_CONNCLOSED	$BATCH_COND( 83, STS$K_WARN)	/* Client closed the connection 				*/

/*
**  Command line parsing, 90 .. 109
*/
#define	BATCH$_IVQUAL		$BATCH_COND( 90, STS$K_ERROR)	/* Unrecognized qualifier 					*/
#define	BATCH$_MISSVAL		$BATCH_COND( 91, STS$K_ERROR)	/* Qualifier requires a value 					*/
#define	BATCH$_IVQVAL		$BATCH_COND( 92, STS$K_ERROR)	/* Invalid qualifier value 					*/
#define	BATCH$_IVPRIO		$BATCH_COND( 93, STS$K_ERROR)	/* Priority out of range 					*/
#define	BATCH$_MAXPARM		$BATCH_COND( 94, STS$K_WARN)	/* Too many parameters 						*/
#define	BATCH$_NOPARAM		$BATCH_COND( 95, STS$K_ERROR)	/* Required parameter missing 					*/
#define	BATCH$_EXTRAPARM	$BATCH_COND( 96, STS$K_ERROR)	/* Extraneous command parameter 				*/
#define	BATCH$_NOSUCHTOPIC	$BATCH_COND(101, STS$K_ERROR)	/* No such help topic */
#define	BATCH$_IVENTRY		$BATCH_COND( 67, STS$K_ERROR)	/* Entry specification is not a number */
#define	BATCH$_PURGED		$BATCH_COND(106, STS$K_SUCCESS)	/* Entries of finished jobs removed */
#define	BATCH$_NOTCONF		$BATCH_COND(107, STS$K_INFO)	/* The operation was not confirmed */
#define	BATCH$_NOTMODIFIABLE	$BATCH_COND(108, STS$K_ERROR)	/* The entry is in a state that forbids the change */
#define	BATCH$_NOTHINGSET	$BATCH_COND(109, STS$K_ERROR)	/* SET ENTRY was given no attribute to set */
#define	BATCH$_QUEORPHAN	$BATCH_COND(105, STS$K_WARN)	/* A queue was restored for orphaned entries */
#define	BATCH$_NOSUCHQUE	$BATCH_COND( 68, STS$K_ERROR)	/* No such queue */
#define	BATCH$_QUEPAUSED	$BATCH_COND( 69, STS$K_SUCCESS)	/* Queue paused */
#define	BATCH$_QUERESUMED	$BATCH_COND(104, STS$K_SUCCESS)	/* Queue resumed */
#define	BATCH$_NOJOBS		$BATCH_COND( 64, STS$K_INFO)	/* No job answers the enquiry */
#define	BATCH$_EMPTYQUE		$BATCH_COND( 65, STS$K_INFO)	/* The queue holds nothing at all */
#define	BATCH$_MGRSTAT		$BATCH_COND( 66, STS$K_SUCCESS)	/* Status of the job controller */
#define	BATCH$_SYNCDONE		$BATCH_COND( 84, STS$K_SUCCESS)	/* The job awaited has completed */
#define	BATCH$_SYNCFAIL		$BATCH_COND( 85, STS$K_ERROR)	/* The job awaited has failed */
#define	BATCH$_SYNCTMO		$BATCH_COND( 86, STS$K_ERROR)	/* The wait timed out */
#define	BATCH$_QUEUED		$BATCH_COND( 46, STS$K_SUCCESS)	/* Job queued, ready to run */
#define	BATCH$_QUEUEDHELD	$BATCH_COND( 47, STS$K_SUCCESS)	/* Job queued and held */
#define	BATCH$_QUEUEDUNTIL	$BATCH_COND( 48, STS$K_SUCCESS)	/* Job queued and held until a time */
#define	BATCH$_RUNASUSER	$BATCH_COND( 44, STS$K_SUCCESS)	/* A job was submitted to run as another user */
#define	BATCH$_NOPRIVUSER	$BATCH_COND( 45, STS$K_ERROR)	/* Not privileged to submit for another user */
#define	BATCH$_NOSUCHUSER	$BATCH_COND(102, STS$K_ERROR)	/* No such user */
#define	BATCH$_IVCPU		$BATCH_COND(100, STS$K_ERROR)	/* Unparsable processor list */
#define	BATCH$_IVVERB		$BATCH_COND( 97, STS$K_ERROR)	/* Unrecognized command 					*/
#define	BATCH$_NOENTRY		$BATCH_COND( 98, STS$K_ERROR)	/* Entry number not specified 					*/
#define	BATCH$_SUBREJECT	$BATCH_COND( 99, STS$K_ERROR)	/* Job rejected by the controller 				*/

/*
**  Process creation and job controller operation, 110 .. 129
*/
#define	BATCH$_FORKERR		$BATCH_COND(110, STS$K_ERROR)	/* Process cannot be created 					*/
#define	BATCH$_DAEMONERR	$BATCH_COND(111, STS$K_FATAL)	/* Cannot detach from the terminal 				*/
#define	BATCH$_PIPEERR		$BATCH_COND(112, STS$K_FATAL)	/* Pipe cannot be created 					*/
#define	BATCH$_POLLERR		$BATCH_COND(113, STS$K_ERROR)	/* Error waiting for an event 					*/
#define	BATCH$_AFFERR		$BATCH_COND(118, STS$K_ERROR)	/* Processor affinity cannot be set */
#define	BATCH$_UNKPROC		$BATCH_COND(114, STS$K_WARN)	/* An unknown process terminated 				*/
#define	BATCH$_IVLCORE		$BATCH_COND(119, STS$K_FATAL)	/* Unusable processor pool */
#define	BATCH$_LCOREPOOL	$BATCH_COND(103, STS$K_SUCCESS)	/* Jobs are spread over a pool of processors */
#define	BATCH$_CTLSTARTED	$BATCH_COND(115, STS$K_SUCCESS)	/* Job controller started 					*/
#define	BATCH$_CTLSTOPPING	$BATCH_COND(116, STS$K_SUCCESS)	/* Job controller is stopping 					*/
#define	BATCH$_IDENT		$BATCH_COND(117, STS$K_SUCCESS)	/* Product identification 					*/

/*
**  Prototype of the message table routine, implemented in module BATCHMSG.C. The table itself and
**  its lookup belong to UTILITY$ROUTINES: the record layout is EMSG_RECORD and the search for a
**  condition is performed by $PUTMSG.
*/
int	msg$init	(void);

#endif								/* __BATCHMSG_H__ 						*/
