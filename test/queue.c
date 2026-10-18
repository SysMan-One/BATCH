/*
**++
**  MODULE:		QUEUE.C
**  IDENT:		X-02-02
**  REV:		02.02.00
**
**  FACILITY:		BATCH
**
**  ABSTRACT:		Regression check on the queue primitives of UTILITY$ROUTINES, upon which the job
**			queue of the controller rests.
**
**			It exists because the library is vendored and is replaced whole when a new
**			revision arrives. $REMQENT once dereferenced the left neighbour of an entry
**			without testing it, so removing the entry at the head of a queue faulted; it
**			also left the right neighbour pointing back at the entry removed and did not
**			re-point the queue when the first or the last entry went. The check walks the
**			queue in both directions after every removal, so a return of any of the three
**			is caught rather than waited for.
**
**			The removals are made in an order that visits every position: the head, the
**			tail, an entry in the middle, and the one entry left at the end.
**
**  AUTHOR:		StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
**
**  CREATION DATE:	29-JUL-2026
**
**  MODIFICATION HISTORY:
**
**	29-JUL-2026	RRL	X-01-01	Initial revision.
**
**	18-OCT-2026	RRL	X-02-02	Revision raised to X-02-02 together with the product; no change of its own.
**--
*/

#include	<stdio.h>

#include	"utility_routines.h"

#undef	__MODULE__
#define	__MODULE__	"QUEUE"
#define	__IDENT__	"X-02-02"
#define	__REV__		"02.02.00"

#define	QUE$K_NITEM	5	/* Number of entries the queue is filled with */

typedef struct queue_item_t {
	ENTRY	qi_links;
	int	qi_id;
} QUE$ITEM;

/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Walks a queue in both directions and reports whether what it holds agrees with what it says it
**	holds. A queue whose links are sound yields the same number of entries either way, and that
**	number is the one the queue itself carries.
**
**  INPUT PARAMETERS:
**	a_que	- the queue, not NULL.
**
**  OUTPUT PARAMETERS:
**	a_fwd	- receives the entries met walking from the head, not NULL.
**	a_bwd	- receives the entries met walking from the tail, not NULL.
**
**  RETURNS:
**	Non-zero if the queue is sound, zero otherwise.
**--
*/
static	int	s_que$walk (
			__QUEUE *	a_que,
			char *		a_fwd,
			char *		a_bwd
			)
{
ENTRY *	l_ent;
char *	l_p;
int	l_nf, l_nb;

	l_p = a_fwd;

	for ( l_nf = 0, l_ent = a_que->head; l_ent; l_ent = l_ent->right, l_nf++ )
		l_p += sprintf(l_p, " %d", ((QUE$ITEM *) l_ent)->qi_id);

	*l_p	= '\0';
	l_p	= a_bwd;

	for ( l_nb = 0, l_ent = a_que->tail; l_ent; l_ent = l_ent->left, l_nb++ )
		l_p += sprintf(l_p, " %d", ((QUE$ITEM *) l_ent)->qi_id);

	*l_p = '\0';

	return	((l_nf == (int) a_que->count) && (l_nb == (int) a_que->count));
}

/*
**++
**  FUNCTIONAL DESCRIPTION:
**	Entry point. Fills a queue, empties it one entry at a time in an order that visits every
**	position, and reports the state after each removal.
**
**  INPUT PARAMETERS:
**	None.
**
**  OUTPUT PARAMETERS:
**	None.
**
**  RETURNS:
**	EXIT_SUCCESS if the queue stayed sound throughout, otherwise EXIT_FAILURE.
**--
*/
int	main (void)
{
__QUEUE		l_que = QUEUE_INITIALIZER;
QUE$ITEM	l_item [QUE$K_NITEM];
unsigned	l_count;
char		l_fwd [64], l_bwd [64];
int		l_i, l_bad;

/* The head, the tail, one in the middle, then what is left */
static const int	s_order [QUE$K_NITEM] = { 0, 4, 2, 1, 3 };

	for ( l_i = 0; l_i < QUE$K_NITEM; l_i++ )
		{
		l_item[l_i].qi_links.left	= NULL;
		l_item[l_i].qi_links.right	= NULL;
		l_item[l_i].qi_links.queue	= NULL;
		l_item[l_i].qi_id		= l_i + 1;

		$INSQTAIL(&l_que, &l_item[l_i], &l_count);
		}

	l_bad = !s_que$walk(&l_que, l_fwd, l_bwd);

	printf("%%BATCH-%s-QUEUE, filled, count=%u forward:%s backward:%s\n",
			(l_bad) ? "E" : "S", l_que.count, l_fwd, l_bwd);

	for ( l_i = 0; l_i < QUE$K_NITEM; l_i++ )
		{
		$REMQENT(&l_que, &l_item[s_order[l_i]], &l_count);

		if ( !s_que$walk(&l_que, l_fwd, l_bwd) )
			l_bad++;

		printf("%%BATCH-%s-QUEUE, removed %d, count=%u forward:%-12s backward:%-12s\n",
				(l_bad) ? "E" : "S", s_order[l_i] + 1, l_que.count, l_fwd, l_bwd);
		}

	if ( l_que.head || l_que.tail || l_que.count )
		{
		printf("%%BATCH-E-QUEUE, the queue is not empty after every entry was removed\n");
		l_bad++;
		}

	printf("%%BATCH-%s-QUEUE, %s\n", (l_bad) ? "F" : "S",
			(l_bad) ? "the queue primitives are defective" : "the queue primitives are sound");

	return	(l_bad) ? 1 : 0;
}
