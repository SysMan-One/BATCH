/*
**++
**
**  FACILITY:	STARLET
**
**  MODULE:	demo.c
**
**  ABSTRACT:	Demonstration of __UTIL$PUTMSG_FAO and __UTIL$PUTMSGD_FAO. It registers a small message table
**		whose texts are FAO control strings and issues them, so that the directives the C
**		run-time library cannot offer are seen at work: the fixed-width field !n<...!>, the
**		zero-filled number !nZL, the counted string !AC and the repetition !n*c.
**
**  AUTHOR:	StarLet squad and Ruslan R. Laishev
**
**  CREATION DATE:  28-JUL-2026
**
**--
*/

#include	<string.h>

#include	"utility_routines.h"

#undef	__MODULE__
#define	__MODULE__	"DEMO"
#define	__IDENT__	"X.01-00"
#define	__REV__		"1.0.00"

/*
**  A facility of our own, its number carrying the bit that marks a number not assigned by the
**  system vendor.
*/
#define	DEMO$K_FACNO		0x0802
#define	$DEMO_COND(msg, sev)	( $DEFFAC(DEMO$K_FACNO) | $DEFMSG(msg) | ((unsigned) (sev) & 0x7) )

#define	DEMO$_NORMAL		$DEMO_COND( 1, STS$K_SUCCESS)
#define	DEMO$_JOBLINE		$DEMO_COND( 2, STS$K_INFO)
#define	DEMO$_ELAPSED		$DEMO_COND( 3, STS$K_SUCCESS)
#define	DEMO$_QUEUE		$DEMO_COND( 4, STS$K_SUCCESS)
#define	DEMO$_RULE		$DEMO_COND( 5, STS$K_SUCCESS)
#define	DEMO$_IOERR		$DEMO_COND( 6, STS$K_ERROR)
#define	DEMO$_ABSENT		$DEMO_COND(99, STS$K_WARN)

#define	$MSGREC(cond, txt)	{ (int) (cond), { (unsigned char) (sizeof(txt) - 1), txt } }

static	EMSG_RECORD	s_msgtab [] =
	{
	$MSGREC(DEMO$_NORMAL,	"%DEMO-S-NORMAL, normal successful completion"),
	$MSGREC(DEMO$_JOBLINE,	"!6UL  !20<!AZ!> !10<!AZ!> !2ZL:!2ZL:!2ZL  !10<!AZ!>"),
	$MSGREC(DEMO$_ELAPSED,	"%DEMO-S-ELAPSED, !UL job!%S completed in !UL second!%S"),
	$MSGREC(DEMO$_QUEUE,	"%DEMO-S-QUEUE, queue !AC holds !UL entr!AZ"),
	$MSGREC(DEMO$_RULE,	" !5*- !20*- !10*- !9*- !10*-"),
	$MSGREC(DEMO$_IOERR,	"%DEMO-E-IOERR, !AZ()->!SL, errno: !UL (!AZ)"),
	{ 0, { 0, "" } }
	};

static	EMSG_RECORD_DESC	s_msgdsc =
	{
	NULL,
	sizeof(s_msgtab) / sizeof(s_msgtab[0]) - 1,
	DEMO$K_FACNO,
	s_msgtab
	};

int	main (void)
{
ASC	l_queue;

	memcpy(l_queue.sts, "SYS$BATCH", 9);
	l_queue.len = 9;

	__util$inimsg(&s_msgdsc);

	$PUTMSG_FAO(DEMO$_NORMAL);

	$PUTMSG_FAO(DEMO$_RULE);

	$PUTMSG_FAO(DEMO$_JOBLINE, (fao_prm_t) 17, (fao_prm_t) "nightly", (fao_prm_t) "Executing",
			(fao_prm_t) 0, (fao_prm_t) 4, (fao_prm_t) 12, (fao_prm_t) "-");

	$PUTMSG_FAO(DEMO$_JOBLINE, (fao_prm_t) 1804, (fao_prm_t) "a-job-with-a-very-long-name",
			(fao_prm_t) "Retained", (fao_prm_t) 1, (fao_prm_t) 2, (fao_prm_t) 3,
			(fao_prm_t) "exit=7");

	$PUTMSG_FAO(DEMO$_ELAPSED, (fao_prm_t) 1, (fao_prm_t) 1);
	$PUTMSG_FAO(DEMO$_ELAPSED, (fao_prm_t) 4, (fao_prm_t) 42);

	$PUTMSG_FAO(DEMO$_QUEUE, (fao_prm_t) &l_queue, (fao_prm_t) 3, (fao_prm_t) "ies");

	$PUTMSG_FAO(DEMO$_IOERR, (fao_prm_t) "read", (fao_prm_t) -1, (fao_prm_t) 5,
			(fao_prm_t) "Input/output error");

	/* A condition absent from the table still names itself */
	$PUTMSG_FAO(DEMO$_ABSENT);

	/* The form without the position of the detection */
	__util$putmsg_fao(DEMO$_NORMAL, NULL, 0);

	return	0;
}
