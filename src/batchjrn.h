/*
**++
**
**  MODULE:		batchjrn.h
**  IDENT:		X-01-01
**  REV:		01.01.00
**
**  FACILITY:		BATCH
**
**  ABSTRACT:		The tags of the fields a job descriptor is written to the queue journal with.
**
**			A record of the journal is not the image of BATCH$JOB but a sequence of tagged
**			values, so that a journal outlives the layout it was written by. A reader takes
**			the tags it knows, passes over those it does not, and leaves at their defaults
**			the fields whose tags are absent. A journal written by an older revision is
**			therefore read by a newer one, and a journal written by a newer revision is read
**			by an older one as far as that older one understands it.
**
**			Three rules keep that property, and they are not negotiable:
**
**			1. A tag, once assigned, is never given to another field. A field withdrawn
**			   leaves its tag retired, listed below as such.
**
**			2. A tag never changes its type. Widening a field means a new tag, the old one
**			   being read for the sake of old journals and no longer written.
**
**			3. The absence of a tag is a meaning of its own - the default of that field - so
**			   a new field must be given a default under which an old journal, which cannot
**			   carry it, still describes a sensible job.
**
**  AUTHOR:		StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
**
**  CREATION DATE:	 2-AUG-2026
**
**  MODIFICATION HISTORY:
**
**	 2-AUG-2026	RRL	X-01-01	Initial revision.
**
**--
*/

#ifndef	__BATCHJRN_H__
#define	__BATCHJRN_H__	1

#undef	__MODULE__
#undef	__IDENT__
#undef	__REV__

#define	__MODULE__	"BATCHJRN"
#define	__IDENT__	"X-01-01"
#define	__REV__		"01.01.00"

#include	"batchdef.h"
#include	"avproto.h"

#define	BATCHJRN$K_SIG		"BATCHJRN"			/* Signature of a record, eight octets exactly		*/
#define	BATCHJRN$K_VERSION	2				/* Format of the journal, not of the descriptor		*/
#define	BATCHJRN$K_RECMAX	8192				/* Ceiling on the size of one encoded record		*/

/*
**  Journal header. The jh_entry field preserves the entry number counter, so that after a restart
**  of the daemon the issue of numbers continues and does not collide with numbers already issued.
**
**  The header states the format of the journal, not the size of a descriptor: a record is a
**  sequence of tagged values rather than the image of BATCH$JOB, so the layout of that structure
**  may change without the journal becoming unreadable. See BATCHJRN.H for the rules the tags are
**  kept by.
*/
typedef struct batchd_jrnhdr_t {
	unsigned int	jh_magic;			/* File signature, always IPC$K_MAGIC 					*/
	unsigned int	jh_version;			/* Format of the journal, BATCHJRN$K_VERSION 				*/
	unsigned int	jh_count;			/* Number of records that follow 					*/
	unsigned int	jh_entry;			/* Preserved value of the entry number counter 				*/
} BATCHD$JRNHDR;

/*
**  The tags. The numbers are grouped by what they describe rather than by the order of the fields
**  in the structure, so that a field added to a group keeps the company of its kind.
*/
enum	{
	JRN$K_ENTRY	= 1,					/* Queue entry number					*/
	JRN$K_STATE,						/* One of the JOB$K_* states				*/
	JRN$K_PRIO,						/* Priority within the queue				*/
	JRN$K_RETAIN,						/* One of the RETAIN$K_* conditions			*/
	JRN$K_RESTARTS,						/* Count of restarts already made			*/

	JRN$K_PID	= 16,					/* Process identifier while executing			*/
	JRN$K_UID,						/* Identity the job runs as				*/
	JRN$K_GID,
	JRN$K_EXIT,						/* Exit status of the interpreter			*/
	JRN$K_SIGNAL,						/* Signal that terminated it, if any			*/

	JRN$K_KEEPLOG	= 32,					/* /KEEP						*/
	JRN$K_RESTART,						/* /RESTART						*/
	JRN$K_LOGCWD,						/* The log belongs beside the submission			*/
	JRN$K_LOGCLIENT,					/* The log was named by the client			*/

	JRN$K_QTIME	= 48,					/* Time queued						*/
	JRN$K_AFTER,						/* Not before this time					*/
	JRN$K_STIME,						/* Time execution started				*/
	JRN$K_ETIME,
	JRN$K_DEV,
	JRN$K_INO,
	JRN$K_MTIME,
	JRN$K_SIZE,						/* Time execution ended					*/

	JRN$K_QUEUE	= 64,					/* Name of the queue					*/
	JRN$K_NAME,						/* Name of the job					*/
	JRN$K_CPUS,						/* Processor list, as written				*/
	JRN$K_SCRIPT,						/* Absolute path of the script				*/
	JRN$K_LOGFILE,						/* Absolute path of the log				*/
	JRN$K_CWD,						/* Directory the job was submitted from			*/
	JRN$K_USER,						/* /USER: whom the job is to run as			*/

	JRN$K_PARM	= 96					/* Parameters: JRN$K_PARM + 0 .. + 7			*/
	};

/*
**  Whether a tag is one this revision assigns. A tag it does not know is not an error - that is the
**  whole point of the format - but ANALYZE reports one so that a journal written by a later revision
**  can be told apart from a journal that is simply damaged.
*/
static	inline	int	batchjrn$known (unsigned a_tag)
{
	switch ( a_tag )
		{
		case	JRN$K_ENTRY:	case	JRN$K_STATE:	case	JRN$K_PRIO:
		case	JRN$K_RETAIN:	case	JRN$K_RESTARTS:
		case	JRN$K_PID:	case	JRN$K_UID:	case	JRN$K_GID:
		case	JRN$K_EXIT:	case	JRN$K_SIGNAL:
		case	JRN$K_KEEPLOG:	case	JRN$K_RESTART:	case	JRN$K_LOGCWD:
		case	JRN$K_LOGCLIENT:
		case	JRN$K_QTIME:	case	JRN$K_AFTER:	case	JRN$K_STIME:
		case	JRN$K_ETIME:
		case	JRN$K_DEV:	case	JRN$K_INO:	case	JRN$K_MTIME:	case	JRN$K_SIZE:
		case	JRN$K_QUEUE:	case	JRN$K_NAME:	case	JRN$K_CPUS:
		case	JRN$K_SCRIPT:	case	JRN$K_LOGFILE:	case	JRN$K_CWD:
		case	JRN$K_USER:
			return	1;
		}

	return	((a_tag >= JRN$K_PARM) && (a_tag < JRN$K_PARM + BATCH$K_NPARM));
}

/*
**  Retired tags, never to be assigned again:
**
**	(none yet)
*/

#endif								/* __BATCHJRN_H__					*/
