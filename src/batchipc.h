/*
**++
**  MODULE:		BATCHIPC.H
**  IDENT:		X-01-06
**  REV:		01.06.00
**
**  FACILITY:		BATCH
**
**  ABSTRACT:		Protocol between the utility BATCH and the job controller BATCHD. The transport
**			is AF_UNIX / SOCK_STREAM. A request or reply frame consists of a fixed-size
**			header BATCH$IPCHDR followed by mh_len bytes of payload. Byte order is not
**			converted, since the exchange takes place solely within one node between images
**			of the same build.
**
**  AUTHOR:		StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
**
**  CREATION DATE:	28-JUL-2026
**
**  MODIFICATION HISTORY:
**
**	28-JUL-2026	RRL	X-01-01	Initial revision. Functions SUBMIT, SHOW, DELETE and RELEASE.
**
**	28-JUL-2026	RRL	X-01-02	Conformance to the coding rules: types MSG_HDR_t and SHOW_REQ_t renamed
**				to BATCH$MSGHDR and BATCH$SHOWREQ, structure tags given the suffix _t.
**
**	28-JUL-2026	RRL	X-01-03	Module renamed from BATCHMSG.H to BATCHIPC.H, constants renamed to
**				IPC$K_*, type renamed to BATCH$IPCHDR. The name BATCHMSG.H was freed for
**				the condition value definitions of the facility, as is customary in
**				OpenVMS for the output of the MESSAGE utility. The mh_sts field now carries
**				a complete condition value rather than an indication of the outcome.
**
**	28-JUL-2026	RRL	X-01-04	Comments converted to English.
**
**	20-AUG-2026	RRL	X-01-05	Function code IPC$K_PURGE added.
**
**	 8-SEP-2026	RRL	X-01-06	Function code IPC$K_SETENTRY and its payload BATCH$SETENT added, for the
**				SET ENTRY command. The payload names the attributes it carries by a mask, so
**				that an attribute not mentioned is not touched: a request cannot reset a
**				field to its default by saying nothing about it.
**--
*/

#ifndef	__BATCHIPC_H__
#define	__BATCHIPC_H__	1

#undef	__MODULE__
#undef	__IDENT__
#undef	__REV__

#define	__MODULE__	"BATCHIPC"
#define	__IDENT__	"X-01-06"
#define	__REV__		"01.06.00"

#include	"batchdef.h"

#define	IPC$K_MAGIC		0x48435442			/* Frame signature, the string "BTCH" little-endian 		*/
#define	IPC$K_SOCKNAME		"batch.sock"			/* Socket file name within the root directory 			*/

/*
**  Function codes. A request and its reply carry the same code and differ only in direction.
*/
#define	IPC$K_SUBMIT		1				/* Place a job in the queue 					*/
#define	IPC$K_SHOW		2				/* Return a selection of jobs 					*/
#define	IPC$K_DELETE		3				/* Abort a job or delete a queue entry 				*/
#define	IPC$K_RELEASE		4				/* Release a job from the holding state 			*/
#define	IPC$K_PURGE		7				/* Remove the entries of jobs that have finished 		*/
#define	IPC$K_QUEUEOP		6				/* Pause or resume a queue 					*/
#define	IPC$K_MGRSTAT		5				/* Return the state of the job controller itself 		*/
#define	IPC$K_SETENTRY		8				/* Alter the attributes of a queue entry 			*/

/*
**  Selection indicators for the IPC$K_SHOW function.
*/
#define	SHOW$M_ALL		(1 << 0)			/* /ALL      - include the pending and holding states 		*/
#define	SHOW$M_FULL		(1 << 1)			/* /FULL     - full format, handled by the utility 		*/
#define	SHOW$M_EVERYONE		(1 << 2)			/* /EVERYONE - jobs belonging to every user 			*/

/*
**  Frame header. Present in every request and in every reply.
*/
typedef struct batch_ipchdr_t {
	unsigned int	mh_magic;				/* Frame signature, always IPC$K_MAGIC 				*/
	unsigned int	mh_func;				/* Function code, one of IPC$K_* 				*/
	unsigned int	mh_sts;					/* Condition value in a reply, see BATCHMSG.H 			*/
	unsigned int	mh_len;					/* Length of the payload in bytes 				*/
	unsigned int	mh_count;				/* Number of BATCH$JOB records in a SHOW reply 			*/
	unsigned int	mh_entry;				/* Entry number: result of SUBMIT, argument of DELETE 		*/
} BATCH$IPCHDR;

/*
**  Payload of an IPC$K_SHOW request.
*/
typedef struct batch_showreq_t {
	unsigned int	sr_flags;				/* Combination of the SHOW$M_* indicators 			*/
	char		sr_queue [BATCH$K_QUESZ];		/* Queue filter, an empty string means none 			*/
	unsigned int	sr_entry;				/* Entry filter, zero means every entry				*/
} BATCH$SHOWREQ;

/*
**  The state of the job controller, returned for IPC$K_MGRSTAT. It answers the question a queue
**  cannot: whether anybody is minding it, since when, and on what terms.
*/
/*
**  One queue of the controller's table, as it stands. It travels with the state of the controller
**  rather than in a request of its own: what one wants to know of a queue - its limit, whether it
**  is paused, and what it is doing with the places it has - is of a piece with what one wants to
**  know of the controller, and two requests would only let the two answers disagree.
*/
typedef struct batch_questat_t {
	char		qs_name [BATCH$K_QUESZ];		/* Name, as it was given				*/
	unsigned int	qs_joblim;				/* Jobs of it that may execute at once			*/
	unsigned int	qs_paused;				/* Non-zero: nothing of it is started			*/
	unsigned int	qs_executing;				/* Counts of its own					*/
	unsigned int	qs_pending;
	unsigned int	qs_holding;
} BATCH$QUESTAT;

/*
**  A request to pause or resume a queue.
*/
#define	QUEOP$K_PAUSE		1
#define	QUEOP$K_RESUME		2

typedef struct batch_queop_t {
	unsigned int	qo_action;				/* One of the QUEOP$K_*					*/
	char		qo_name [BATCH$K_QUESZ];		/* The queue it applies to				*/
} BATCH$QUEOP;

typedef struct batch_mgrstat_t {
	unsigned int	ms_pid;					/* Process identifier of the controller			*/
	unsigned int	ms_joblim;				/* Jobs that may execute at one time			*/
	unsigned int	ms_executing;				/* Counts by state, as they stand			*/
	unsigned int	ms_pending;
	unsigned int	ms_holding;
	unsigned int	ms_aborting;
	unsigned int	ms_retained;
	unsigned int	ms_entry;				/* Next entry number to be issued			*/
	unsigned int	ms_logkeep;				/* Days a job log left behind is kept			*/
	unsigned int	ms_lcorecnt;				/* Processors set aside by /LCORE, zero for none		*/

	long long	ms_stime;				/* Time the controller started				*/

	char		ms_ident [16];				/* Identification of the controller image		*/
	char		ms_version [16];
	char		ms_queue [BATCH$K_QUESZ];		/* Queue it serves					*/
	char		ms_root [BATCH$K_PATHSZ];		/* Root directory of the subsystem			*/
	char		ms_lcore [BATCH$K_CPUSZ];		/* The pool, as it was written				*/

	unsigned int	ms_quecnt;				/* Queues the controller was told of			*/
	BATCH$QUESTAT	ms_queues [BATCH$K_MAXQUE];
} BATCH$MGRSTAT;

/*
**  A request to alter the attributes of a queue entry, function IPC$K_SETENTRY. The entry number
**  travels in the header, as it does for DELETE and RELEASE.
**
**  The mask names the attributes the request carries; a field whose indicator is clear is not looked
**  at and the entry keeps what it had. Where a qualifier has a negated form, the indicator says the
**  attribute is to be set and the field says to what: /HOLD and /NOHOLD both set SETENT$M_HOLD and
**  differ in se_hold, and /NOAFTER is SETENT$M_AFTER with se_after of zero.
*/
#define	SETENT$M_PRIORITY	(1 << 0)			/* se_prio 						*/
#define	SETENT$M_AFTER		(1 << 1)			/* se_after, zero clears the time 			*/
#define	SETENT$M_HOLD		(1 << 2)			/* se_hold, non-zero holds and zero releases 		*/
#define	SETENT$M_NAME		(1 << 3)			/* se_name 						*/
#define	SETENT$M_RETAIN		(1 << 4)			/* se_retain, one of RETAIN$K_* 			*/
#define	SETENT$M_KEEP		(1 << 5)			/* se_keep 						*/
#define	SETENT$M_RESTART	(1 << 6)			/* se_restart 						*/
#define	SETENT$M_CPU		(1 << 7)			/* se_cpus 						*/

/*
**  The attributes that concern what becomes of a job once it has run, and may therefore be altered
**  while it runs. Everything else describes how and when a job is to start, and is settled once it
**  has.
*/
#define	SETENT$M_WHILERUN	(SETENT$M_RETAIN | SETENT$M_KEEP | SETENT$M_RESTART)

typedef struct batch_setent_t {
	unsigned int	se_mask;				/* Combination of the SETENT$M_* indicators 		*/
	unsigned int	se_prio;				/* /PRIORITY 						*/
	unsigned int	se_retain;				/* /RETAIN 						*/

	int		se_hold;				/* /HOLD is one, /NOHOLD is zero 			*/
	int		se_keep;				/* /KEEP is one, /NOKEEP is zero 			*/
	int		se_restart;				/* /RESTART is one, /NORESTART is zero 			*/

	long long	se_after;				/* /AFTER in seconds of the epoch, /NOAFTER is zero 	*/

	char		se_name [BATCH$K_NAMESZ];		/* /NAME 						*/
	char		se_cpus [BATCH$K_CPUSZ];		/* /CPU 						*/
} BATCH$SETENT;

/*
**  Renders the attributes a request carries as a list a reader can check against what was typed,
**  "priority=200, hold, after=..." - for the confirmation of the utility and the log of the
**  controller alike, so that the two say the same thing. Implemented in module UTIL.C.
*/
int	util$fmt_setent		(const BATCH$SETENT *a_req, char *a_buf, size_t a_bufsz);

#endif								/* __BATCHIPC_H__ 						*/
