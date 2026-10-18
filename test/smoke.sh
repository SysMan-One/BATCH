#!/bin/bash
#++
#  MODULE:		SMOKE.SH
#  IDENT:		X-01-26
#  REV:			01.26.00
#
#  FACILITY:		BATCH
#
#  ABSTRACT:		Functional test of the subsystem. It brings up a job controller on a temporary
#			root directory and verifies: placing a job in the queue, observance of the limit
#			on simultaneously executing jobs, retention of the entry of an abnormally
#			terminated job, removal of the entry of a successful one, the holding state
#			produced by the /HOLD and /AFTER qualifiers, aborting a job, survival of the
#			queue across a restart of the job controller, and the issue of condition values.
#
#  PARAMETERS:		$1 - full specification of the BATCH image
#			$2 - full specification of the BATCHD image
#
#  RETURNS:		0 - every check passed, 1 - a discrepancy was found.
#
#  AUTHOR:		StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
#
#  CREATION DATE:	28-JUL-2026
#
#  MODIFICATION HISTORY:
#
#	28-JUL-2026	RRL	X-01-01	Initial revision.
#
#	28-JUL-2026	RRL	X-01-02	Fixed delays replaced by waiting for the expected state of the queue:
#				on a loaded machine a job did not always finish within the second
#				allowed, and the checks reported a spurious discrepancy. A check of the
#				issue of condition values was added.
#
#	28-JUL-2026	RRL	X-01-03	Messages and match patterns converted to English.
#
#	28-JUL-2026	RRL	X-01-04	A check of the delta forms of /AFTER that carry no day field was added:
#				a defect in the parser left "+3", "+:30" and "+::45" unrecognized while
#				the only form exercised by the test, "+1-", kept working.
#
#	28-JUL-2026	RRL	X-01-05	Adjusted to the message facility of UTILITY$ROUTINES: a condition
#				relayed by the utility now carries the value, and its text is looked for
#				in the log of the job controller.
#
#	28-JUL-2026	RRL	X-01-06	The job controller is started with the qualifiers of the CLI services
#				instead of the former single-letter options.
#
#	29-JUL-2026	RRL	X-01-07	The jobs the test starts are put down on cleanup. A job is a session of
#				its own and outlives the job controller, so those that sleep were lingering
#				after the test and loading the machine for whatever ran next.
#
#	29-JUL-2026	RRL	X-01-08	A check of the /RESTART qualifier was added.
#
#	29-JUL-2026	RRL	X-01-09	A check that a silent client does not hold up the controller was added.
#
#	29-JUL-2026	RRL	X-01-10	Checks of the log sweep and of the /CPU qualifier were added.
#
#	29-JUL-2026	RRL	X-01-11	A check that a change which starts nothing still reaches the journal was
#				added: the indication that the queue had changed was being discarded.
#
#	 1-AUG-2026	RRL	X-01-12	A check of the HELP command was added.
#
#	 1-AUG-2026	RRL	X-01-13	A check that an abbreviated verb reaches its own command was added.
#
#	 1-AUG-2026	RRL	X-01-14	A check that the submission names the time a held job starts was added.
#
#	 2-AUG-2026	RRL	X-01-15	A check of the log location and of /KEEP was added: the log had stopped
#				being created at all and no check noticed.
#
#	 2-AUG-2026	RRL	X-01-16	A check that SHOW honours an entry number was added.
#
#	 2-AUG-2026	RRL	X-01-17	A check that times are local, on both sides, was added.
#
#	 2-AUG-2026	RRL	X-01-18	A check that an unknown tag in a journal record is passed over.
#
#	 2-AUG-2026	RRL	X-01-19	A check of the ANALYZE command was added.
#
#	 2-AUG-2026	RRL	X-01-20	A check that the log holds the whole of a run was added.
#
#	 2-AUG-2026	RRL	X-01-21	A check of the /USER qualifier was added.
#
#	31-AUG-2026	RRL	X-01-22	A check that the three help libraries hold the same topics. A
#					translation falls behind silently - the command answers, in the wrong
#					language - and it had fallen behind twice before anyone noticed.
#
#	31-AUG-2026	RRL	X-01-23	And a check that the three User's Guides speak of the same commands and
#					conditions. Their prose is not compared - a translation is not a
#					transliteration - but the names a reader looks up must be in all three.
#					It found two conditions missing from the French the first time it ran.
#
#	 8-SEP-2026	RRL	X-01-24	A check of the SET ENTRY command: an attribute altered in place, the
#					negated forms, what the state of a job allows, and a command that names
#					nothing.
#
#	 8-OCT-2026	RRL	X-01-25	SHOW without /ALL displays the jobs that wait: check 3 now expects the held
#					job in the ordinary display, and check 2 is joined by one of a successful
#					job kept by /RETAIN=ALWAYS, which only /ALL displays.
#
#	18-OCT-2026	RRL	X-01-26	The check of DELETE looks for the refused element in the text of IVENTRY:
#					the text carried a printf conversion the FAO engine printed as it stood,
#					and the mnemonic alone did not show it. A static check is added that every
#					message text and the message reference of the manuals are written for the
#					FAO engine and that every diagnostic goes through $PUTMSG_FAO.
#--
#

set -u

g_batch="${1:?the specification of the BATCH image is required}"
g_batchd="${2:?the specification of the BATCHD image is required}"

#
#  The root of the sources, so that the help library built beside them can be found. Optional: a
#  test run by hand needs only the two images.
#
g_srcdir="${3:-.}"

g_root="$(mktemp -d /tmp/batch-smoke.XXXXXX)"
g_pid=""
g_fail=0

export BATCH_ROOT="${g_root}"

#
#  Stop the job controller and remove the temporary directory whatever the outcome.
#
l_cleanup ()
{
	local l_p l_i

	if [ -n "${g_pid}" ] ; then
		kill "${g_pid}" 2>/dev/null
		wait "${g_pid}" 2>/dev/null
	fi

	#
	#  A job is a session of its own and outlives the job controller, so the ones this test started
	#  have to be put down explicitly; check 7 orphans some of them on purpose, and those the queue
	#  no longer knows at all. The signal goes to the process group rather than to the interpreter
	#  alone, since what sleeps is its child. A job is the leader of its own group, so its
	#  identifier serves as the identifier of the group - the same property the DELETE command
	#  relies upon. The children of the interpreter are taken first and the interpreter after them,
	#  by identifier: a negative number naming a group is read by KILL as an option of its own.
	#
	#  The sweep is made twice: a job the controller had just created may not yet appear in the
	#  process list at the moment of the first one.
	#
	for l_i in 1 2 ; do
		for l_p in $(pgrep -f "${g_root}/scripts/" 2>/dev/null) ; do
			pkill -P "${l_p}" 2>/dev/null
			kill "${l_p}" 2>/dev/null
		done

		sleep 0.2
	done

	rm -rf "${g_root}"
}

trap l_cleanup EXIT

#
#  Record the result of one check.
#
l_check ()
{
	if [ "$1" = "0" ] ; then
		echo "%BATCH-S-SMOKE, $2"
	else
		echo "%BATCH-E-SMOKE, $2"
		g_fail=1
	fi
}

#
#  Wait for the socket file to appear, for at most five seconds.
#
l_wait_sock ()
{
	local l_i

	for l_i in $(seq 1 50) ; do
		[ -S "${g_root}/batch.sock" ] && return 0
		sleep 0.1
	done

	return 1
}

#
#  Wait for a pattern to appear in the output of a command. This replaces a fixed delay: the time a
#  job takes to run depends on the load of the machine and is not known in advance.
#
#  $1 - the pattern sought, followed by the arguments of the BATCH command.
#
l_wait_for ()
{
	local l_i l_pat

	l_pat="$1"
	shift

	for l_i in $(seq 1 100) ; do
		"${g_batch}" "$@" 2>/dev/null | grep -q "${l_pat}" && return 0
		sleep 0.1
	done

	return 1
}

#
#  Wait for a pattern to disappear from the output of a command.
#
#  $1 - the pattern, followed by the arguments of the BATCH command.
#
l_wait_gone ()
{
	local l_i l_pat

	l_pat="$1"
	shift

	for l_i in $(seq 1 100) ; do
		"${g_batch}" "$@" 2>/dev/null | grep -q "${l_pat}" || return 0
		sleep 0.1
	done

	return 1
}

#
#  Start the job controller in the foreground, retaining its process identifier.
#
l_start ()
{
	"${g_batchd}" /FOREGROUND /VERBOSE "/ROOT=${g_root}" /JOB_LIMIT=2 >>"${g_root}/batchd.log" 2>&1 &
	g_pid=$!
	l_wait_sock
}

echo "%BATCH-I-SMOKE, test root directory ${g_root}"

mkdir -p "${g_root}/scripts"

cat > "${g_root}/scripts/ok.sh" <<'EOF'
echo "P1=${P1} entry=${BATCH_ENTRY}"
exit 0
EOF

cat > "${g_root}/scripts/fail.sh" <<'EOF'
echo "about to fail"
exit 7
EOF

cat > "${g_root}/scripts/long.sh" <<'EOF'
sleep 30
EOF

if ! l_start ; then
	echo "%BATCH-F-SMOKE, the job controller did not come up"
	exit 1
fi

l_check 0 "job controller started, pid=${g_pid}"

#
#  Check 1. A job with a nonzero exit status is retained in the queue and is visible without /ALL.
#
"${g_batch}" submit "${g_root}/scripts/fail.sh" /NAME=badjob >/dev/null 2>&1
l_wait_for "exit=7" show
l_check $? "abnormally terminated job retained with exit=7"

#
#  Check 2. A successful job is removed from the queue and is not visible even with /ALL.
#
"${g_batch}" submit "${g_root}/scripts/ok.sh" /NAME=goodjob '/PARAMETERS=(alpha)' >/dev/null 2>&1
l_wait_gone "goodjob" show /all
l_check $? "successful job removed from the queue"

#
#  Check 2a. A successful job kept by /RETAIN=ALWAYS is displayed only with /ALL: there is nothing in
#  its entry to be acted upon, and an ordinary SHOW is the queue as it stands to be worked.
#
"${g_batch}" submit "${g_root}/scripts/ok.sh" /NAME=keptjob /RETAIN=ALWAYS '/PARAMETERS=(alpha)' >/dev/null 2>&1
l_wait_for "keptjob .*exit=0" show /all
"${g_batch}" show      > "${g_root}/show2a.txt" 2>&1
"${g_batch}" show /all > "${g_root}/show2b.txt" 2>&1

grep -q "keptjob" "${g_root}/show2b.txt" && ! grep -q "keptjob" "${g_root}/show2a.txt"
l_check $? "successful job kept by /RETAIN=ALWAYS shown only with /ALL"

#
#  Check 3. The /HOLD qualifier leaves a job in the holding state, displayed with and without /ALL.
#
l_ent=$("${g_batch}" submit "${g_root}/scripts/long.sh" /NAME=heldjob /HOLD 2>/dev/null \
		| sed -n 's/.*entry \([0-9]*\).*/\1/p')

"${g_batch}" show /all > "${g_root}/show3.txt" 2>&1
"${g_batch}" show      > "${g_root}/show4.txt" 2>&1

grep -q "heldjob" "${g_root}/show3.txt" && grep -q "Holding" "${g_root}/show3.txt" \
	&& grep "heldjob" "${g_root}/show4.txt" | grep -q "Holding"
l_check $? "job submitted with /HOLD is holding and shown without /ALL"

#
#  Check 4. An /AFTER time in the future likewise yields the holding state.
#
"${g_batch}" submit "${g_root}/scripts/ok.sh" /NAME=laterjob '/AFTER=+1-' >/dev/null 2>&1
l_wait_for "laterjob" show /all
"${g_batch}" show /all > "${g_root}/show5.txt" 2>&1

grep "laterjob" "${g_root}/show5.txt" | grep -q "Holding"
l_check $? "job submitted with a future /AFTER is holding"

#
#  Check 5. Observance of the job limit: with /JOB_LIMIT=2, two of three jobs execute and one waits.
#
"${g_batch}" submit "${g_root}/scripts/long.sh" /NAME=lj1 >/dev/null 2>&1
"${g_batch}" submit "${g_root}/scripts/long.sh" /NAME=lj2 >/dev/null 2>&1
"${g_batch}" submit "${g_root}/scripts/long.sh" /NAME=lj3 >/dev/null 2>&1
l_wait_for "lj2 *Executing" show /all
"${g_batch}" show /all > "${g_root}/show6.txt" 2>&1

l_exec=$(grep -c "Executing" "${g_root}/show6.txt")
l_pend=$(grep -c "Pending" "${g_root}/show6.txt")

[ "${l_exec}" = "2" ] && [ "${l_pend}" = "1" ]
l_check $? "job limit observed: Executing=${l_exec}, Pending=${l_pend}"

#
#  Check 6. Removal of a held entry by the DELETE command.
#
"${g_batch}" delete "${l_ent}" >/dev/null 2>&1
"${g_batch}" show /all > "${g_root}/show7.txt" 2>&1

! grep -q "heldjob" "${g_root}/show7.txt"
l_check $? "entry ${l_ent} removed by DELETE"

#
#  Check 7. Survival across a restart of the job controller: the entry of the abnormally terminated
#  job is to remain, and the jobs that were executing are to become retained as orphans.
#
kill "${g_pid}" 2>/dev/null
wait "${g_pid}" 2>/dev/null
g_pid=""

l_start
l_wait_for "badjob" show /all
"${g_batch}" show /all > "${g_root}/show8.txt" 2>&1

grep -q "badjob" "${g_root}/show8.txt" && grep -q "laterjob" "${g_root}/show8.txt"
l_check $? "queue restored from the journal after a restart"

#
#  Check 8. Command line parsing: an absolute path is not to be taken for a qualifier, and an
#  unrecognized qualifier is to be rejected.
#
"${g_batch}" submit "${g_root}/scripts/ok.sh" /NAME=pathjob >/dev/null 2>&1
l_sts1=$?
"${g_batch}" submit "${g_root}/scripts/ok.sh" /NOSUCHQUAL=1 >/dev/null 2>&1
l_sts2=$?

[ "${l_sts1}" = "0" ] && [ "${l_sts2}" != "0" ]
l_check $? "absolute path taken as a parameter, unrecognized qualifier rejected"

#
#  Check 9. Delta forms of /AFTER that carry no day field. They differ from the form used above in
#  that the day field and its separating dash are absent altogether.
#
l_naft=0

for l_spec in "+3" "+:30" "+::45" ; do
	"${g_batch}" submit "${g_root}/scripts/ok.sh" "/NAME=aft${l_naft}" "/AFTER=${l_spec}" >/dev/null 2>&1 || break
	l_wait_for "aft${l_naft} *Holding" show /all || break
	l_naft=$((l_naft + 1))
done

[ "${l_naft}" = "3" ]
l_check $? "delta forms +3, +:30 and +::45 accepted and left holding"

#
#  Check 10. Diagnostics are issued as condition values of the facility: the mnemonic comes from the
#  message table of the library. A condition produced by the job controller is reported by the utility
#  as a value, while its readable text appears in full in the log of the job controller.
#
"${g_batch}" delete 99999 > "${g_root}/cond1.txt" 2>&1
"${g_batch}" submit /no/such/script.sh > "${g_root}/cond2.txt" 2>&1

grep -q "%BATCH-E-SUBREJECT" "${g_root}/cond1.txt" \
	&& grep -q "%BATCH-E-NOSUCHENT" "${g_root}/batchd.log" \
	&& grep -q "%BATCH-E-NOSCRIPT" "${g_root}/cond2.txt"
l_check $? "conditions of the utility and of the controller issued with their mnemonics"

#
#  Check 11. The /RESTART qualifier. A job so submitted goes back into the queue when the controller
#  is started again, whereas one without it is kept as orphaned.
#
#  The queue still holds the jobs of check 5, and with a limit of two the new ones would only be
#  pending. It is emptied first so that both of them actually run.
#
for l_e in $("${g_batch}" show /all 2>/dev/null | awk '/^ *[0-9]+ / { print $1 }') ; do
	"${g_batch}" delete "${l_e}" >/dev/null 2>&1
done

l_wait_gone "lj[123]" show /all

"${g_batch}" submit "${g_root}/scripts/long.sh" /NAME=durable /RESTART >/dev/null 2>&1
"${g_batch}" submit "${g_root}/scripts/long.sh" /NAME=fragile >/dev/null 2>&1
l_wait_for "durable *Executing" show /all
l_wait_for "fragile *Executing" show /all

kill "${g_pid}" 2>/dev/null
wait "${g_pid}" 2>/dev/null
g_pid=""

l_start
l_wait_for "durable" show /all
"${g_batch}" show /all > "${g_root}/show9.txt" 2>&1

grep "durable" "${g_root}/show9.txt" | grep -qE "Pending|Executing" \
	&& grep "fragile" "${g_root}/show9.txt" | grep -q "Retained" \
	&& grep -q "%BATCH-S-JOBREQUEUED" "${g_root}/batchd.log"
l_check $? "job submitted with /RESTART requeued, one without it kept as orphaned"

#
#  Check 12. A client that connects and then says nothing must hold up nobody but itself. The
#  connection is opened by a helper, since the shell cannot open a socket of the UNIX domain; where
#  no interpreter for it is to be found the check is passed over rather than failed.
#
if command -v python3 >/dev/null 2>&1 ; then
	cat > "${g_root}/mute.py" <<EOF
import socket, time
s = socket.socket(socket.AF_UNIX)
s.connect("${g_root}/batch.sock")
time.sleep(5)
s.close()
EOF
	python3 "${g_root}/mute.py" &
	l_mute=$!
	sleep 0.5

	l_t0=$(date +%s)
	"${g_batch}" submit "${g_root}/scripts/ok.sh" /NAME=served >/dev/null 2>&1
	l_sts=$?
	l_t1=$(date +%s)

	kill "${l_mute}" 2>/dev/null
	wait "${l_mute}" 2>/dev/null

	[ "${l_sts}" = "0" ] && [ $((l_t1 - l_t0)) -lt 3 ]
	l_check $? "a silent client does not hold up the controller"
else
	echo "%BATCH-I-SMOKE, no python3 to open a socket with, the silent client is not exercised"
fi

#
#  Check 13. The log directory is swept of what belongs to no entry, and the /CPU qualifier is
#  verified when the command is entered.
#
touch -d "10 days ago" "${g_root}/log/stale_999.log" 2>/dev/null || touch "${g_root}/log/stale_999.log"

kill "${g_pid}" 2>/dev/null
wait "${g_pid}" 2>/dev/null
g_pid=""
l_start

l_i=0

while [ "${l_i}" -lt 30 ] ; do
	[ -f "${g_root}/log/stale_999.log" ] || break
	sleep 0.1
	l_i=$((l_i + 1))
done

[ ! -f "${g_root}/log/stale_999.log" ]
l_check $? "a job log left behind is swept from the log directory"

"${g_batch}" submit "${g_root}/scripts/ok.sh" /CPU=0 >/dev/null 2>&1
l_sts1=$?
"${g_batch}" submit "${g_root}/scripts/ok.sh" /CPU=0-x >/dev/null 2>&1
l_sts2=$?

[ "${l_sts1}" = "0" ] && [ "${l_sts2}" != "0" ]
l_check $? "a well formed processor list is accepted and a malformed one rejected"

#
#  Check 14. A change that the scheduler does not follow with one of its own must reach the journal
#  all the same. A job placed with /HOLD starts nothing, so nothing else marks the queue as changed.
#
"${g_batch}" submit "${g_root}/scripts/long.sh" /NAME=solitary /HOLD >/dev/null 2>&1
l_wait_for "solitary" show /all

kill "${g_pid}" 2>/dev/null
wait "${g_pid}" 2>/dev/null
g_pid=""
l_start
l_wait_for "solitary" show /all

"${g_batch}" show /all 2>/dev/null | grep -q "solitary"
l_check $? "a queued job that starts nothing survives a restart of the controller"

#
#  Check 15. The HELP command answers out of the help library of the subsystem. The library is
#  looked for beside the images and in the current directory as well, so a build tree that has not
#  been installed can be exercised; where the library was not built at all - a system without
#  HELPTRAN - the check is passed over rather than failed.
#
if [ -f "${g_srcdir}/doc/batch.hlb" ] ; then
	l_here=`pwd`
	cd "${g_srcdir}/doc" || exit 1

	case "${g_batch}" in
	/*)	l_img="${g_batch}" ;;
	*)	l_img="${l_here}/${g_batch}" ;;
	esac

	A=`"${l_img}" help submit 2>&1`
	B=`"${l_img}" help submit /after 2>&1`
	C=`"${l_img}" help nosuchtopic 2>&1`

	cd "${l_here}" || exit 1

	echo "${A}" | grep -q "Queues a shell script" \
		&& echo "${B}" | grep -q "before which the job is not to start" \
		&& echo "${C}" | grep -q "%BATCH-E-NOSUCHTOPIC"
	l_check $? "the HELP command answers a topic, a subtopic and refuses an unknown one"
else
	echo "%BATCH-I-SMOKE, no help library was built, the HELP command is not exercised"
fi

#
#  Check 16. An abbreviated verb reaches the command it abbreviates. The CLI services accept an
#  unambiguous leading portion, and a dispatch that resolved the verb a second time of its own
#  disagreed with them: every abbreviation fell through to the last branch, so DEL released the
#  entry it was meant to delete.
#
"${g_batch}" subm "${g_root}/scripts/long.sh" /NAME=abbrev /HOLD >/dev/null 2>&1
l_sts1=$?
l_wait_for "abbrev *Holding" show /all

l_e=`"${g_batch}" show /all 2>/dev/null | awk '/abbrev/ { print $1 }'`

"${g_batch}" del "${l_e}" >/dev/null 2>&1
l_sts2=$?
l_wait_gone "abbrev" show /all

"${g_batch}" show /all 2>/dev/null | grep -q "abbrev"
l_sts3=$?

[ "${l_sts1}" = "0" ] && [ "${l_sts2}" = "0" ] && [ "${l_sts3}" != "0" ]
l_check $? "an abbreviated verb reaches the command it abbreviates"

#
#  Check 17. The submission names the time a held job is to start. A delta specification is easy to
#  write and hard to read back - "+7" is seven hours and "+7-" is seven days - so a job held for the
#  wrong span is indistinguishable from one held for the right span until the span has passed.
#
A=`"${g_batch}" submit "${g_root}/scripts/long.sh" /NAME=until7h '/AFTER=+7' 2>&1`
B=`"${g_batch}" submit "${g_root}/scripts/long.sh" /NAME=until7d '/AFTER=+7-' 2>&1`
C=`"${g_batch}" submit "${g_root}/scripts/long.sh" /NAME=heldonly /HOLD 2>&1`

echo "${A}" | grep -q "holding until" && echo "${B}" | grep -q "holding until" \
	&& [ "`echo \"${A}\" | sed 's/.*holding until //'`" != "`echo \"${B}\" | sed 's/.*holding until //'`" ] \
	&& echo "${C}" | grep -q "QUEUEDHELD"
l_check $? "the submission names the time a held job is to start"

#
#  The same time appears in the summary listing, so that finding out when a job starts does not
#  call for /FULL. A job held without a time, and one that has run, keep what the column held
#  before: what became of them.
#
D=`"${g_batch}" show /all 2>/dev/null`

echo "${D}" | awk '/until7d/ { print $NF }' | grep -q ':' \
	&& echo "${D}" | awk '/heldonly/ { print $NF }' | grep -q '^-$'
l_check $? "the summary listing names the time a held job is to start"

#
#  Check 18. The log of a job goes into the directory it was submitted from, as it does in OpenVMS,
#  and /KEEP preserves it there. The log is opened by the controller before it assumes the identity
#  of the submitter, so its absence is not a permission of the submitter's but a defect: a job that
#  ran and wrote nothing anywhere means the descriptor never named the file.
#
l_here=`pwd`
mkdir -p "${g_root}/subdir"
cd "${g_root}/subdir" || exit 1

case "${g_batch}" in
/*)	l_img="${g_batch}" ;;
*)	l_img="${l_here}/${g_batch}" ;;
esac

BATCH_ROOT="${g_root}" "${l_img}" submit "${g_root}/scripts/ok.sh" /NAME=logkeep /KEEP >/dev/null 2>&1
BATCH_ROOT="${g_root}" "${l_img}" submit "${g_root}/scripts/ok.sh" /NAME=lognokeep    >/dev/null 2>&1

l_n=0
while [ "${l_n}" -lt 25 ] ; do
	[ -f "${g_root}/subdir/logkeep_"*.log ] && break
	l_n=`expr ${l_n} + 1`
	sleep 0.4
done

sleep 1

ls "${g_root}/subdir/logkeep_"*.log >/dev/null 2>&1
l_sts1=$?
ls "${g_root}/subdir/lognokeep_"*.log >/dev/null 2>&1
l_sts2=$?

cd "${l_here}" || exit 1

[ "${l_sts1}" = "0" ] && [ "${l_sts2}" != "0" ]
l_check $? "the log is written where the job was submitted from, and /KEEP preserves it"

#
#  Check 19. SHOW displays the entry it is given and nothing else. An entry named this way is shown
#  whatever state it holds, so that SHOW of an entry does not depend upon /ALL as SHOW of the queue
#  does; an entry that does not exist is a condition rather than an empty display, the enquirer
#  having asked about one entry and being owed an answer about that entry.
#
"${g_batch}" submit "${g_root}/scripts/long.sh" /NAME=seen /HOLD >/dev/null 2>&1
"${g_batch}" submit "${g_root}/scripts/long.sh" /NAME=unseen /HOLD >/dev/null 2>&1
l_wait_for "unseen" show /all

l_e=`"${g_batch}" show /all 2>/dev/null | awk '/ seen /  { print $1 }'`

O=`"${g_batch}" show "${l_e}" 2>&1`
"${g_batch}" show 99999 >/dev/null 2>&1
l_sts=$?

echo "${O}" | grep -q "seen" && ! echo "${O}" | grep -q "unseen" && [ "${l_sts}" != "0" ]
l_check $? "SHOW displays the entry it is given, and refuses one that does not exist"

#
#  Check 20. Times are shown by the reader's clock, and an absolute /AFTER is read by it too. The
#  two go together: were only one of them local, a time entered and then displayed would come back
#  altered, which is worse than either convention taken on its own.
#
l_h=`TZ=UTC date -u '+%Y'`
l_when="31-DEC-${l_h}:23:30"

TZ=Etc/GMT-5 "${g_batch}" submit "${g_root}/scripts/long.sh" /NAME=tzjob "/AFTER=${l_when}" > "${g_root}/tz.txt" 2>&1
l_wait_for "tzjob" show /all

l_shown=`TZ=Etc/GMT-5 "${g_batch}" show /all /full 2>/dev/null | awk '/Not before/ { print $4, $5 }'`
l_utc=`TZ=UTC "${g_batch}" show /all /full 2>/dev/null | awk '/Not before/ { print $4, $5 }'`

#  Read and shown in one zone, the reading comes back unaltered; read in one and shown in another,
#  it differs by the five hours between them.
echo "${l_shown}" | grep -q "23:30:00" && [ "${l_shown}" != "${l_utc}" ]
l_check $? "a time is shown by the clock of whoever reads it, and read by his clock too"

#
#  Check 21. A record of the journal describes itself, so a tag the reader does not know is passed
#  over rather than ending the load. That is what lets a journal cross a revision of the software in
#  either direction, and it is tested by forging a record with a tag no revision assigns: a reader
#  that stumbles over it would lose the whole queue.
#
"${g_batch}" submit "${g_root}/scripts/long.sh" /NAME=survivor /HOLD /PRIORITY=177 >/dev/null 2>&1
l_wait_for "survivor" show /all

kill "${g_pid}" 2>/dev/null
wait "${g_pid}" 2>/dev/null
g_pid=""

#  A tag of 4000 belongs to no revision; it is appended to the first record of the journal
python3 - "${g_root}/journal.dat" <<'EOF_PY' 2>/dev/null
import struct, sys

p = sys.argv[1]
d = bytearray(open(p, 'rb').read())

hdr = 16                                        #  magic, version, count, entry
rl  = struct.unpack_from('<I', d, hdr)[0]       #  length of the first record
rec = bytearray(d[hdr + 4 : hdr + 4 + rl])

tlv = struct.pack('>HH', (0 << 12) | 4000, 4) + b'\xde\xad\xbe\xef'
ulen = struct.unpack_from('>I', rec, 8)[0]      #  payload length in the record header
struct.pack_into('>I', rec, 8, ulen + len(tlv))
rec += tlv

d[hdr : hdr + 4 + rl] = struct.pack('<I', len(rec)) + rec
open(p, 'wb').write(bytes(d))
EOF_PY

#
#  A copy is taken before the controller is started again: loading the journal and writing it back
#  is what drops the forged tag, which is precisely the behaviour under test.
#
cp "${g_root}/journal.dat" "${g_root}/tags.dat"

l_start
l_wait_for "survivor" show /all

"${g_batch}" show /all /full 2>/dev/null | grep -A4 "job survivor" | grep -q "177"
l_check $? "a record carrying an unknown tag is still read, the tag being passed over"

#
#  ANALYZE names such a tag and shows its value, so a journal written by a later revision can be
#  told apart from one quietly corrupted. It is not counted a fault: passing over what one does not
#  know is the whole point of the format.
#
"${g_batch}" analyze "${g_root}/tags.dat" > "${g_root}/tags.txt" 2>&1
l_sts=$?

grep -q "tag 4000" "${g_root}/tags.txt" && grep -q "DE AD BE EF" "${g_root}/tags.txt" \
	&& grep -q "no fault found" "${g_root}/tags.txt" && [ "${l_sts}" = "0" ]
l_check $? "ANALYZE names an unknown tag and shows its octets, counting it no fault"

#
#  Check 22. ANALYZE reads the journal directly, so it serves where the controller will not start -
#  which is when it is wanted most - and it says so when the file is damaged rather than pronouncing
#  it sound.
#
"${g_batch}" submit "${g_root}/scripts/long.sh" /NAME=analysed /HOLD >/dev/null 2>&1
l_wait_for "analysed" show /all

kill "${g_pid}" 2>/dev/null
wait "${g_pid}" 2>/dev/null
g_pid=""

BATCH_ROOT="${g_root}" "${g_batch}" analyze /dump > "${g_root}/an.txt" 2>&1
l_sts1=$?

cp "${g_root}/journal.dat" "${g_root}/bad.dat"
printf '\377\377\000\000' | dd of="${g_root}/bad.dat" bs=1 seek=20 conv=notrunc >/dev/null 2>&1

"${g_batch}" analyze "${g_root}/bad.dat" > "${g_root}/bad.txt" 2>&1
l_sts2=$?

l_start

[ "${l_sts1}" = "0" ] && grep -q "no fault found" "${g_root}/an.txt" \
	&& grep -q "analysed" "${g_root}/an.txt" \
	&& [ "${l_sts2}" != "0" ] && grep -q "fault" "${g_root}/bad.txt"
l_check $? "ANALYZE reports a sound journal as sound and a damaged one as damaged"

#
#  Check 25. The log of the controller holds the whole of a run, the identification and the loading
#  of the journal included, and not merely the part that follows the detaching; the terminal, which
#  is let go at that point, is told the outcome and where to read the rest.
#
rm -rf "${g_root}/lgrun"
mkdir -p "${g_root}/lgrun"

"${g_batchd}" "/ROOT=${g_root}/lgrun" > "${g_root}/lg.out" 2> "${g_root}/lg.err"
l_sts1=$?
sleep 1

l_pid2=`pgrep -f "ROOT=${g_root}/lgrun" | head -1`

#  A second controller on the same root cannot start, and must say so on the terminal
"${g_batchd}" "/ROOT=${g_root}/lgrun" > /dev/null 2> "${g_root}/lg2.err"
l_sts2=$?

#
#  "pkill batchd && batchd" is an ordinary way to restart the thing, and a controller that has been
#  signalled holds its socket until it has finished stopping: the successor must wait for that
#  rather than refuse at once.
#
kill "${l_pid2}" 2>/dev/null && "${g_batchd}" "/ROOT=${g_root}/lgrun" > /dev/null 2>&1
l_sts3=$?
sleep 1

l_pid3=`pgrep -f "ROOT=${g_root}/lgrun" | head -1`
[ -n "${l_pid3}" ] && kill "${l_pid3}" 2>/dev/null

[ "${l_sts3}" = "0" ] \
	&& [ "${l_sts1}" = "0" ] && grep -q "CTLSTARTED" "${g_root}/lgrun/batchd.log" \
	&& grep -q "IDENT" "${g_root}/lgrun/batchd.log" \
	&& grep -q "CTLSTARTED" "${g_root}/lg.err" \
	&& [ "${l_sts2}" != "0" ] && grep -q "CTLNOSTART" "${g_root}/lg2.err"
l_check $? "the log holds a whole run, the terminal is told, and a restart is not refused"

#
#  Check 26. /USER runs a job as somebody else, and only the privileged user may ask for it: the
#  utility runs as whoever invoked it and could claim anything, so the identity is settled by the
#  controller from what the kernel reports about the sender.
#
if [ "`id -u`" = "0" ] ; then
	#  The root of the test is private to its owner, so the job itself will not get far; what is
	#  under test is whom it was to run as, and the log the controller made over to that user says
	#  so whether the job ran or not.
	"${g_batch}" submit "${g_root}/scripts/ok.sh" /NAME=asuser /USER=nobody /KEEP >/dev/null 2>&1

	l_n=0
	while [ "${l_n}" -lt 25 ] ; do
		[ -f "${g_root}/log/asuser_"*.log ] 2>/dev/null && break
		l_n=`expr ${l_n} + 1`
		sleep 0.4
	done

	l_own=`stat -c '%U' "${g_root}/log/asuser_"*.log 2>/dev/null | head -1`

	"${g_batch}" submit "${g_root}/scripts/ok.sh" /USER=nosuchperson >"${g_root}/nou.txt" 2>&1
	l_sts1=$?

	#  The root of the test is private to its owner, so passage through it is granted for the sake
	#  of this one request: what is under test is the refusal, not the reaching of the socket.
	chmod 711 "${g_root}"
	chmod 755 "${g_root}/scripts"
	chmod 644 "${g_root}/scripts/ok.sh"

	su -s /bin/sh nobody -c "BATCH_ROOT=${g_root} ${g_batch} submit ${g_root}/scripts/ok.sh /USER=root" \
		>"${g_root}/nopriv.txt" 2>&1
	l_sts2=$?

	chmod 700 "${g_root}"

	[ "${l_own}" = "nobody" ] && [ "${l_sts1}" != "0" ] && grep -q "NOSUCHUSER" "${g_root}/nou.txt" \
		&& [ "${l_sts2}" != "0" ] && grep -q "NOPRIVUSER" "${g_root}/nopriv.txt"
	l_check $? "/USER runs a job as another user, and only the privileged user may ask for it"

	#
	#  Granting one user the authority of another is the one operation of this subsystem that
	#  hands out an authority rather than uses one, so the log is to hold a record of every such
	#  grant and of every refusal - naming who asked, and for whom.
	#
	grep -q "RUNASUSER.*uid 0 to run as nobody" "${g_root}/batchd.log" \
		&& grep -q "NOPRIVUSER.*to run as root" "${g_root}/batchd.log"
	l_check $? "the log records who asked to run a job as another user, and for whom"

	#
	#  A job submitted to run as somebody else belongs to that somebody at once, so its submitter
	#  would not see the entry he had just been given the number of - were the privileged user
	#  filtered by ownership as everybody else is. He may delete and release any entry, and an
	#  entry he may dispose of but cannot see is worse than useless.
	#
	"${g_batch}" submit "${g_root}/scripts/long.sh" /NAME=theirs /USER=nobody /HOLD >/dev/null 2>&1
	l_wait_for "theirs" show /all

	l_e=`"${g_batch}" show /all 2>/dev/null | awk '/theirs/ { print $1 }'`

	"${g_batch}" show "${l_e}" 2>/dev/null | grep -q "theirs"
	l_check $? "the privileged user sees an entry that belongs to another"
fi

#
#  /PAGE is a courtesy to a reader at a terminal and a nuisance to a program reading a pipe, so it
#  is granted only where there is a terminal to hold at: redirected, it must leave the output
#  exactly as it was, prompts and all absent.
#
P=`"${g_batch}" show /all /page 2>&1`
Q=`"${g_batch}" show /all 2>&1`

[ "${P}" = "${Q}" ]
l_check $? "/PAGE is accepted and leaves redirected output untouched"

if [ "1" = "0" ] ; then
	:
else
	echo "%BATCH-I-SMOKE, not the privileged user, /USER is not exercised"
fi

#
#  SYNC waits for a job and carries the answer in its exit status, so a script that has work to do
#  after a job need not read a display to learn how it went.
#
#  The number is taken from the confirmation: a job this short is gone from the queue before a
#  display could be asked for, which is precisely what SYNC is to report as success.
l_e1=`"${g_batch}" submit "${g_root}/scripts/ok.sh" /NAME=syncok 2>&1 | sed -n 's/.*as entry //p'`

"${g_batch}" sync "${l_e1}" >/dev/null 2>&1
l_sts1=$?

l_e2=`"${g_batch}" submit "${g_root}/scripts/fail.sh" /NAME=syncbad 2>&1 | sed -n 's/.*as entry //p'`

"${g_batch}" sync "${l_e2}" > "${g_root}/sy.txt" 2>&1
l_sts2=$?

l_e3=`"${g_batch}" submit "${g_root}/scripts/long.sh" /NAME=synclong 2>&1 | sed -n 's/.*as entry //p'`

"${g_batch}" sync "${l_e3}" /TIMEOUT=2 > "${g_root}/sy2.txt" 2>&1
l_sts3=$?

[ "${l_sts1}" = "0" ] && [ "${l_sts2}" != "0" ] && grep -q "SYNCFAIL" "${g_root}/sy.txt" \
	&& [ "${l_sts3}" != "0" ] && grep -q "SYNCTMO" "${g_root}/sy2.txt"
l_check $? "SYNC waits for a job and carries its outcome in the exit status"

#
#  A log named by the client is his own affair and never the controller's: opened under the identity
#  of the job rather than under the privilege of the daemon, and never unlinked. Otherwise any user
#  who can reach the socket could have the privileged user create, truncate or take away whatever
#  file he cared to name.
#
if [ "`id -u`" = "0" ] ; then
	chmod 711 "${g_root}"
	chmod 755 "${g_root}/scripts"
	chmod 644 "${g_root}/scripts/ok.sh"

	echo "untouched" > "${g_root}/victim"
	chmod 600 "${g_root}/victim"

	su -s /bin/sh nobody -c \
	  "BATCH_ROOT=${g_root} ${g_batch} submit ${g_root}/scripts/ok.sh /NAME=evil /LOG_FILE=${g_root}/victim" \
		>/dev/null 2>&1

	sleep 2
	chmod 700 "${g_root}"

	grep -q "untouched" "${g_root}/victim" 2>/dev/null
	l_check $? "a log named by a client is neither created, truncated nor removed by the daemon"
else
	echo "%BATCH-I-SMOKE, not the privileged user, the log ownership check is not exercised"
fi


#
#  A path says nothing about the file it names for longer than it takes to say it. A script replaced
#  between the submission and the running is not the script that was submitted, and the job is not
#  to run it.
#
#  A controller of its own is used: by this point the slots of the main one are taken by the long
#  jobs the earlier checks left running, and what is under test is not the scheduler.
#
mkdir -p "${g_root}/swaprun"
cp "${g_root}/scripts/ok.sh" "${g_root}/scripts/swap.sh"

"${g_batchd}" /FOREGROUND "/ROOT=${g_root}/swaprun" >"${g_root}/swaprun/log.txt" 2>&1 &
l_spid=$!

l_n=0
while [ "${l_n}" -lt 25 ] ; do
	[ -S "${g_root}/swaprun/batch.sock" ] && break
	l_n=`expr ${l_n} + 1`
	sleep 0.4
done

l_e=`BATCH_ROOT="${g_root}/swaprun" "${g_batch}" submit "${g_root}/scripts/swap.sh" /NAME=swapped /HOLD 2>&1 \
	| sed -n 's/.*as entry \([0-9][0-9]*\).*/\1/p'`

printf 'exit 0\n' > "${g_root}/scripts/swap.sh"

BATCH_ROOT="${g_root}/swaprun" "${g_batch}" release "${l_e}" >/dev/null 2>&1

l_n=0
while [ "${l_n}" -lt 25 ] ; do
	BATCH_ROOT="${g_root}/swaprun" "${g_batch}" show /all 2>/dev/null | grep -q "exit=-1" && break
	l_n=`expr ${l_n} + 1`
	sleep 0.4
done

BATCH_ROOT="${g_root}/swaprun" "${g_batch}" show /all 2>/dev/null | grep "swapped" | grep -q "exit=-1" \
	&& grep -q "SCRIPTCHG" "${g_root}/swaprun/log.txt"
l_sts=$?


kill "${l_spid}" 2>/dev/null
wait "${l_spid}" 2>/dev/null

[ "${l_sts}" = "0" ]
l_check $? "a script replaced after submission is refused when the job comes to be run"

#
#  The name of a job becomes part of the specification of its log, and that log is the one file the
#  controller opens while it still holds its privileges. A name carrying a path separator or a
#  parent reference would lead that open out of the log directory.
#
"${g_batch}" submit "${g_root}/scripts/ok.sh" "/NAME=../../escaped" > "${g_root}/esc.txt" 2>&1
l_sts1=$?

"${g_batch}" submit "${g_root}/scripts/ok.sh" "/NAME=a/b" > "${g_root}/esc2.txt" 2>&1
l_sts2=$?

[ "${l_sts1}" != "0" ] && grep -q "LOGSPEC" "${g_root}/esc.txt" \
	&& [ "${l_sts2}" != "0" ] && grep -q "LOGSPEC" "${g_root}/esc2.txt"
l_check $? "a job name that would lead the log out of its directory is refused"

#
#  No two conditions may share a number. A message is looked up by it, so a collision hands the
#  arguments of one message to the text of another - and a text expecting a string where a number
#  was passed takes the process down. It cost a crash in SYNC to learn.
#
l_dup=`grep -oE '[$]BATCH_COND\( *[0-9]+' "${g_srcdir}/src/batchmsg.h" \
	| grep -oE '[0-9]+' | sort -n | uniq -d | wc -l`

[ "${l_dup}" = "0" ]
l_check $? "no two conditions share a message number"

#
#  SHOW/MANAGER says whether anybody is minding the queue. A queue that stands still looks the same
#  whether the controller is busy, idle or gone.
#
"${g_batch}" show /manager > "${g_root}/mgr.txt" 2>&1
l_sts=$?

[ "${l_sts}" = "0" ] && grep -q "Job controller of queue" "${g_root}/mgr.txt" \
	&& grep -q "Job limit" "${g_root}/mgr.txt" \
	&& grep -q "pid " "${g_root}/mgr.txt"
l_check $? "SHOW/MANAGER reports the state of the job controller"

#
#  DELETE and RELEASE take a list of entries. Taking the first number and passing over the rest
#  without a word was the worst of the possible answers: the command reported success for the one
#  entry it did dispose of.
#
l_a=`"${g_batch}" submit "${g_root}/scripts/long.sh" /NAME=lista /HOLD 2>&1 \
	| sed -n 's/.*as entry \([0-9][0-9]*\).*/\1/p'`
l_b=`"${g_batch}" submit "${g_root}/scripts/long.sh" /NAME=listb /HOLD 2>&1 \
	| sed -n 's/.*as entry \([0-9][0-9]*\).*/\1/p'`
l_c=`"${g_batch}" submit "${g_root}/scripts/long.sh" /NAME=listc /HOLD 2>&1 \
	| sed -n 's/.*as entry \([0-9][0-9]*\).*/\1/p'`

"${g_batch}" delete "${l_a},${l_b},${l_c}" > "${g_root}/lst.txt" 2>&1
l_sts1=$?

l_gone=0
"${g_batch}" show /all 2>/dev/null | grep -q "lista" || l_gone=`expr ${l_gone} + 1`
"${g_batch}" show /all 2>/dev/null | grep -q "listb" || l_gone=`expr ${l_gone} + 1`
"${g_batch}" show /all 2>/dev/null | grep -q "listc" || l_gone=`expr ${l_gone} + 1`

"${g_batch}" delete "1,notanumber" > "${g_root}/lst2.txt" 2>&1
l_sts2=$?

[ "${l_sts1}" = "0" ] && [ "${l_gone}" = "3" ] \
	&& [ "${l_sts2}" != "0" ] && grep -q 'IVENTRY, "notanumber" does not name' "${g_root}/lst2.txt"
l_check $? "DELETE takes a list of entries and refuses an element that is not one"

#
#  A shell that ends because the command it ran was killed exits with 128 plus the signal. The job
#  did not die of a signal itself, so no other part of the display would say so, and 135 is not a
#  thing anyone knows by sight.
#
cat > "${g_root}/scripts/bus.sh" <<'EOF_BUS'
sh -c 'kill -BUS $$'
EOF_BUS

#  A controller of its own: the slots of the main one are taken by the long jobs of earlier checks
mkdir -p "${g_root}/busrun"

"${g_batchd}" /FOREGROUND "/ROOT=${g_root}/busrun" >"${g_root}/busrun/log.txt" 2>&1 &
l_bpid=$!

l_n=0
while [ "${l_n}" -lt 25 ] ; do
	[ -S "${g_root}/busrun/batch.sock" ] && break
	l_n=`expr ${l_n} + 1`
	sleep 0.4
done

BATCH_ROOT="${g_root}/busrun" "${g_batch}" submit "${g_root}/scripts/bus.sh" /NAME=signalled \
	>/dev/null 2>&1

l_n=0
while [ "${l_n}" -lt 25 ] ; do
	grep -q "128 + 7" "${g_root}/busrun/log.txt" && break
	l_n=`expr ${l_n} + 1`
	sleep 0.4
done

grep -q "128 + 7" "${g_root}/busrun/log.txt"
l_sts=$?

kill "${l_bpid}" 2>/dev/null
wait "${l_bpid}" 2>/dev/null

[ "${l_sts}" = "0" ]
l_check $? "an exit status above 128 is reported with the signal it implies"

#
#  Queues have limits of their own: jobs of one queue run in the order their priorities give, and
#  jobs of different queues run beside one another. A queue the controller was never told of is
#  refused at submission - a job placed in one nobody minds would wait for ever, and would look
#  exactly like a job waiting its turn.
#
mkdir -p "${g_root}/querun"

"${g_batchd}" /FOREGROUND "/ROOT=${g_root}/querun" "/QUEUE=(SYS\$BATCH,slow:1,fast:3)" \
	>"${g_root}/querun/log.txt" 2>&1 &
l_qpid=$!

l_n=0
while [ "${l_n}" -lt 25 ] ; do
	[ -S "${g_root}/querun/batch.sock" ] && break
	l_n=`expr ${l_n} + 1`
	sleep 0.4
done

for l_i in 1 2 3 ; do
	BATCH_ROOT="${g_root}/querun" "${g_batch}" submit "${g_root}/scripts/long.sh" \
		"/NAME=s${l_i}" /QUEUE=slow >/dev/null 2>&1
	BATCH_ROOT="${g_root}/querun" "${g_batch}" submit "${g_root}/scripts/long.sh" \
		"/NAME=f${l_i}" /QUEUE=fast >/dev/null 2>&1
done

sleep 2

l_slow=`BATCH_ROOT="${g_root}/querun" "${g_batch}" show /all 2>/dev/null \
	| grep -c "s[0-9] *Executing"`
l_fast=`BATCH_ROOT="${g_root}/querun" "${g_batch}" show /all 2>/dev/null \
	| grep -c "f[0-9] *Executing"`

BATCH_ROOT="${g_root}/querun" "${g_batch}" submit "${g_root}/scripts/ok.sh" /QUEUE=nowhere \
	>"${g_root}/noque.txt" 2>&1
l_sts=$?

	#
	#  A paused queue starts nothing, and its entries are not disturbed: that is what pausing is
	#  for. SHOW/QUEUES carries the table in the same reply as the state of the controller, so the
	#  two cannot disagree.
	#
	BATCH_ROOT="${g_root}/querun" "${g_batch}" queue slow /PAUSE >/dev/null 2>&1

	BATCH_ROOT="${g_root}/querun" "${g_batch}" submit "${g_root}/scripts/long.sh" \
		/NAME=paused /QUEUE=slow >/dev/null 2>&1
	sleep 2

	l_state=`BATCH_ROOT="${g_root}/querun" "${g_batch}" show /all 2>/dev/null \
		| awk '/paused/ { print $3 }'`
	l_shown=`BATCH_ROOT="${g_root}/querun" "${g_batch}" show /queues 2>/dev/null \
		| awk '/^ slow/ { print $NF }'`

	[ "${l_state}" = "Pending" ] && [ "${l_shown}" = "paused" ]
	l_check $? "a paused queue starts nothing and says so in SHOW/QUEUES"

kill "${l_qpid}" 2>/dev/null
wait "${l_qpid}" 2>/dev/null

[ "${l_slow}" = "1" ] && [ "${l_fast}" = "3" ] \
	&& [ "${l_sts}" != "0" ] && grep -q "NOSUCHQUE" "${g_root}/noque.txt"
l_check $? "each queue has a limit of its own, and an unknown queue is refused"


#
#  A qualifier is /name or --name, and nothing else. The single hyphen was withdrawn by the CLI
#  services, and this utility must agree with them: were an argument taken for a qualifier here and
#  refused as a name there, the diagnostic would speak of something the user did not write.
#
A=`"${g_batch}" submit "${g_root}/scripts/ok.sh" /NAME=slashform 2>&1`
B=`"${g_batch}" submit "${g_root}/scripts/ok.sh" --NAME=gnuform 2>&1`
C=`"${g_batch}" submit "${g_root}/scripts/ok.sh" -NAME=hyphen 2>&1`
l_sts=$?

echo "${A}" | grep -q "slashform" && echo "${B}" | grep -q "gnuform" \
	&& [ "${l_sts}" != "0" ] && echo "${C}" | grep -q "EXTRAPARM"
l_check $? "a qualifier is /name or --name, and a lone hyphen is an ordinary value"

#
#  PURGE removes the entries of jobs that have finished and leaves alone what has not. Confirmation
#  is asked where /CONFIRM asks for it, and anything but a word beginning with Y is a refusal: the
#  operation cannot be undone, and silence is not consent.
#
#  A controller of its own: the slots of the main one are taken by the long jobs of earlier checks
mkdir -p "${g_root}/pgrun"

"${g_batchd}" /FOREGROUND "/ROOT=${g_root}/pgrun" >"${g_root}/pgrun/log.txt" 2>&1 &
l_ppid=$!

l_n=0
while [ "${l_n}" -lt 25 ] ; do
	[ -S "${g_root}/pgrun/batch.sock" ] && break
	l_n=`expr ${l_n} + 1`
	sleep 0.4
done

export BATCH_ROOT="${g_root}/pgrun"

"${g_batch}" submit "${g_root}/scripts/fail.sh" /NAME=purgeme >/dev/null 2>&1
l_wait_for "purgeme .*exit=" show /all

"${g_batch}" submit "${g_root}/scripts/long.sh" /NAME=purgestay /HOLD >/dev/null 2>&1
l_wait_for "purgestay" show /all

printf 'n\n' | "${g_batch}" purge /CONFIRM > "${g_root}/pg1.txt" 2>&1
l_kept=`"${g_batch}" show /all 2>/dev/null | grep -c "purgeme"`

printf 'y\n' | "${g_batch}" purge /CONFIRM > "${g_root}/pg2.txt" 2>&1
l_gone=`"${g_batch}" show /all 2>/dev/null | grep -c "purgeme"`
l_stay=`"${g_batch}" show /all 2>/dev/null | grep -c "purgestay"`

kill "${l_ppid}" 2>/dev/null
wait "${l_ppid}" 2>/dev/null
export BATCH_ROOT="${g_root}"

grep -q "NOTCONF" "${g_root}/pg1.txt" && [ "${l_kept}" = "1" ] \
	&& grep -q "PURGED" "${g_root}/pg2.txt" && [ "${l_gone}" = "0" ] && [ "${l_stay}" = "1" ]
l_check $? "PURGE removes what has finished, spares what has not, and honours a refusal"

#
#  SET ENTRY alters a queued job in place. The entry keeps its number; the priority, the name and the
#  start time change; /NOAFTER clears a time and /NOHOLD lets a held job go; an executing job admits
#  only what concerns its disposition afterwards, a retained one nothing; and a command that names no
#  attribute is refused before anything is sent.
#
#  A controller of its own, as for PURGE: the slots of the main one are taken by earlier checks.
#
mkdir -p "${g_root}/serun"

"${g_batchd}" /FOREGROUND "/ROOT=${g_root}/serun" >"${g_root}/serun/log.txt" 2>&1 &
l_spid=$!

l_n=0
while [ "${l_n}" -lt 25 ] ; do
	[ -S "${g_root}/serun/batch.sock" ] && break
	l_n=`expr ${l_n} + 1`
	sleep 0.4
done

export BATCH_ROOT="${g_root}/serun"

l_ent=$("${g_batch}" submit "${g_root}/scripts/long.sh" /NAME=setme /HOLD 2>/dev/null \
		| sed -n 's/.*entry \([0-9]*\).*/\1/p')
"${g_batch}" submit "${g_root}/scripts/fail.sh" /NAME=setdone >/dev/null 2>&1
l_wait_for "setdone .*exit=" show /all

"${g_batch}" set entry "${l_ent}" /PRIORITY=200 /NAME=renamed '/AFTER=+1-' > "${g_root}/se1.txt" 2>&1
"${g_batch}" show "${l_ent}" /all /full > "${g_root}/se2.txt" 2>&1

l_ok=0
grep -q "ENTMODIFIED, entry ${l_ent} modified: priority=200, after=.*, name=renamed" "${g_root}/se1.txt" \
	&& grep -q "job renamed" "${g_root}/se2.txt" && grep -q "Priority ......... 200" "${g_root}/se2.txt" \
	&& grep -q "Holding" "${g_root}/se2.txt" && grep -q "Not before" "${g_root}/se2.txt" && l_ok=1

#  /HOLD /NOAFTER: the time goes and the job stays held; /NOHOLD alone then lets it go
"${g_batch}" set entry "${l_ent}" /HOLD /NOAFTER > "${g_root}/se3.txt" 2>&1
"${g_batch}" show "${l_ent}" /all /full > "${g_root}/se4.txt" 2>&1
"${g_batch}" set entry "${l_ent}" /NOHOLD > "${g_root}/se5.txt" 2>&1
l_wait_for "renamed .*Executing" show /all

grep -q "modified: noafter, hold" "${g_root}/se3.txt" && grep -q "Holding" "${g_root}/se4.txt" \
	&& ! grep -q "Not before" "${g_root}/se4.txt" && grep -q "modified: nohold" "${g_root}/se5.txt" \
	|| l_ok=0

#  Executing: the priority is refused and the retention accepted; retained: everything refused
"${g_batch}" set entry "${l_ent}" /PRIORITY=5 > "${g_root}/se6.txt" 2>&1
"${g_batch}" set entry "${l_ent}" /RETAIN=ALWAYS /NOKEEP > "${g_root}/se7.txt" 2>&1
l_done=`"${g_batch}" show /all 2>/dev/null | sed -n 's/^ *\([0-9]*\) *setdone .*/\1/p'`
"${g_batch}" set entry "${l_done}" /PRIORITY=5 > "${g_root}/se8.txt" 2>&1
"${g_batch}" set entry "${l_ent}" > "${g_root}/se9.txt" 2>&1

grep -q "NOTMODIFIABLE" "${g_root}/se6.txt" && grep -q "modified: retain=always, nokeep" "${g_root}/se7.txt" \
	&& grep -q "NOTMODIFIABLE" "${g_root}/se8.txt" && grep -q "NOTHINGSET" "${g_root}/se9.txt" \
	&& cat "${g_root}/serun/log.txt" "${g_root}/serun/batchd.log" 2>/dev/null | grep -q "entry ${l_ent} is Executing" \
	&& cat "${g_root}/serun/log.txt" "${g_root}/serun/batchd.log" 2>/dev/null | grep -q "entry ${l_done} is Retained" \
	|| l_ok=0

kill "${l_spid}" 2>/dev/null
wait "${l_spid}" 2>/dev/null
export BATCH_ROOT="${g_root}"

[ "${l_ok}" = "1" ]
l_check $? "SET ENTRY alters a queued job in place and honours the state of the job"

#
#  The three help libraries are to hold the same topics. A translation falls behind silently: the
#  command answers, in the wrong language, and nobody notices until somebody reads it. The names of
#  the topics are compared rather than their number - a count agrees just as readily when one
#  library has gained a topic and another has lost one.
#
#  Only the keys are compared, and only their spelling: a topic is named the same in every language
#  because it is the word the user types.
#
for l_lang in rus fra ; do
	sed -n 's/^#\{1,2\} \([^ ]*\).*/\1/p' "${g_srcdir}/doc/batch_help.md" | sort > "${g_root}/keys.en"
	sed -n 's/^#\{1,2\} \([^ ]*\).*/\1/p' "${g_srcdir}/doc/batch_help_${l_lang}.md" | sort \
		> "${g_root}/keys.${l_lang}"

	if ! cmp -s "${g_root}/keys.en" "${g_root}/keys.${l_lang}" ; then
		echo "%BATCH-E-SMOKE, doc/batch_help_${l_lang}.md does not hold the topics of the English one:"
		diff "${g_root}/keys.en" "${g_root}/keys.${l_lang}" | head -10
		g_fail=1
	fi
done

[ "${g_fail}" = "0" ]
l_check $? "the three help libraries hold the same topics"

#
#  And the manuals are to speak of the same things. Their prose is not compared - a translation is
#  not a transliteration, and the French says in four lines what the English says in six - but the
#  names a reader looks up must be in all three: every command, and every condition mnemonic of the
#  message reference. A manual that has fallen behind is missing a name, not a paragraph.
#
for l_lang in rus fra ; do
	l_missing=""

	for l_name in SUBMIT SHOW DELETE RELEASE PURGE SYNC QUEUE ANALYZE "SET ENTRY" \
			PURGED NOTCONF NOSUCHQUE SCRIPTCHG QUEORPHAN SYNCFAIL SYNCTMO \
			ENTMODIFIED NOTMODIFIABLE NOTHINGSET ; do
		grep -q "${l_name}" "${g_srcdir}/doc/batch_users_guide_${l_lang}.md" \
			|| l_missing="${l_missing} ${l_name}"
	done

	if [ -n "${l_missing}" ] ; then
		echo "%BATCH-E-SMOKE, doc/batch_users_guide_${l_lang}.md does not speak of:${l_missing}"
		g_fail=1
	fi
done

[ "${g_fail}" = "0" ]
l_check $? "the three User's Guides speak of the same commands and conditions"

#
#  Diagnostics are rendered by the FAO engine, which does not know a printf conversion and prints it
#  as it stands. Nothing compiles the texts against their calls, so a conversion left behind shows
#  only when the message is issued, as IVENTRY once printed "%s". The table, the message reference
#  of the English and Russian manuals (the French one names the values in words) and the call sites
#  are looked at; a !%S, !%D or !%T of the FAO engine is not a printf conversion.
#
g_pct='(^|[^!%])%[-+ #0-9.]*(ll|l|z|h)?[sdiuxXc]'

grep -nE "^[[:space:]]*\\\$MSGREC\(.*${g_pct}" "${g_srcdir}/src/batchmsg.c" > "${g_root}/fao.txt"

for l_lang in "" _rus ; do
	sed -n '/^## 6\.3 /,/^## [0-9]/p' "${g_srcdir}/doc/batch_users_guide${l_lang}.md" \
		| grep -nE "^    [^*].*${g_pct}" | sed "s|^|batch_users_guide${l_lang}.md:|" >> "${g_root}/fao.txt"
done

grep -nE '[$](PUTMSG|PUTMSGD|LOG|LOGD)[[:space:]]*\(|__util[$](putmsg|putmsgd|log|logd)[[:space:]]*\(' \
	"${g_srcdir}"/src/*.c "${g_srcdir}"/src/*.h | grep -vE ':[0-9]+:[*][*]' >> "${g_root}/fao.txt"

if [ -s "${g_root}/fao.txt" ] ; then
	echo "%BATCH-E-SMOKE, a message is not written for the FAO engine:"
	head -10 "${g_root}/fao.txt"
	g_fail=1
fi

[ "${g_fail}" = "0" ]
l_check $? "every message text is an FAO control string and is issued by \$PUTMSG_FAO"

if [ "${g_fail}" = "0" ] ; then
	echo "%BATCH-S-SMOKE, every check passed"
else
	echo "%BATCH-F-SMOKE, discrepancies were found, log: ${g_root}/batchd.log"
	cat "${g_root}/batchd.log"
fi

exit "${g_fail}"
