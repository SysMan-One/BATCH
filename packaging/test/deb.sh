#!/bin/bash
#++
#	FACILITY: BATCH - the tests of the kits
#
#	USAGE:
#		# packaging/test/deb.sh <ident> <directory with the .deb kits>
#
#	DESCRIPTION: Installs the .deb kit of THIS processor into the Debian or
#		Ubuntu it runs on (a container in CI, as root) and checks, step
#		by step, what an operator relies on:
#
#		    1. apt installs the kit; the postinst starts the job
#		       controller - in a container without systemd through the
#		       System V script it copies into /etc/init.d
#		    2. the files are there, the images are static ("not a dynamic
#		       executable"), the spool directory exists
#		    3. the controller runs and prints the %BATCH-S-IDENT of the tag
#		    4. a job goes through: SUBMIT, SYNC, its log
#		    5. the NSS part of a static glibc image works on THIS
#		       distribution's glibc: /USER=nobody runs a job as nobody,
#		       the owner is named in SHOW /FULL
#		       (and, last but one, that an unknown user is refused by
#		       NOSUCHUSER and does not kill the controller - a static
#		       image of glibc dies there, see PACKAGING/BUILD.SH)
#		    6. HELP answers (the library is installed and found)
#		    7. a second install over the first restarts the controller and
#		       the held job is still in the queue (the journal)
#		    8. dpkg --remove stops the controller and takes the programs
#		       away but keeps the spool; dpkg --purge takes the spool too
#
#		Not tested: a start of the service under systemd - that needs
#		a host, not a container.
#
#	AUTHOR: StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
#
#	CREATION DATE: 04-NOV-2026
#
#	MODIFICATION HISTORY:
#
#		04-NOV-2026	RRL	X-02-02ECO03 : Initial revision, after BroTwins' PACKAGING/TEST/DEB.SH.
#--

set -u

IDENT="${1:?the ident of the tag}"
DIR="${2:?the directory of the kits}"
FAILED=0

PREFIX=/usr/local
SPOOL=/var/spool/batch
BATCH=${PREFIX}/bin/batch
BATCHD=${PREFIX}/sbin/batchd

#  apt downloads as the user _apt, who may not read a mounted directory: work on a copy
KITS=$(mktemp -d)
cp "${DIR}"/*.deb "${KITS}/" && chmod -R a+rX "${KITS}"
DIR="${KITS}"

#  The jobs of /USER=nobody must reach their script: a directory of its own, mode 755
JOBS=$(mktemp -d)
chmod 755 "${JOBS}"

ok ()	{ echo "ok    : $*"; }
bad ()	{ echo "FAILED: $*"; FAILED=1; }
check ()
{
local	what="$1"
	shift
	if "$@" >/dev/null 2>&1; then ok "${what}"; else bad "${what}"; fi
}

#  <what> <command...>: the command is tried for 20 s, every 0.5 s
wait_for ()
{
local	what="$1" n=0
	shift
	while [ ${n} -lt 40 ]
	do
		if "$@" >/dev/null 2>&1; then ok "${what}"; return 0; fi
		n=$((n + 1)); sleep 0.5
	done
	bad "${what}"
	return 1
}

#  <script> <qualifiers...>: submits, prints the entry number
submit ()
{
local	script="$1" out
	shift
	out=$(cd "${JOBS}" && "${BATCH}" submit "${script}" "$@" 2>&1)
	echo "${out}" >&2
	echo "${out}" | sed -n 's/.*entry \([0-9][0-9]*\).*/\1/p' | head -1
}

export DEBIAN_FRONTEND=noninteractive

ARCH=$(dpkg --print-architecture)
echo "== $(. /etc/os-release && echo "${PRETTY_NAME}"), ${ARCH}, tag ${IDENT}"

deb=$(ls "${DIR}"/batch_*_"${ARCH}".deb 2>/dev/null | head -1)
echo "== batch: ${deb}"
[ -n "${deb}" ] || { bad "no .deb for ${ARCH} in ${DIR}"; exit 1; }

printf '#!/bin/sh\nid -un\nid -Gn\n' >"${JOBS}/who.sh"
chmod 755 "${JOBS}/who.sh"

#  1.
out=$(apt-get install -y -q "${deb}" 2>&1); rc=$?
echo "${out}" | tail -5
[ ${rc} -eq 0 ] && ok "apt install" || { bad "apt install"; exit 1; }

#  2.
check "batch"				test -x ${BATCH}
check "batchd"				test -x ${BATCHD}
check "help library, English"		test -f ${PREFIX}/share/help/batch.hlb
check "help library, Russian"		test -f ${PREFIX}/share/help/ru/batch_rus.hlb
check "help library, French"		test -f ${PREFIX}/share/help/fr/batch_fra.hlb
check "systemd unit"			test -f /usr/lib/systemd/system/batchd.service
check "System V script"			test -x ${PREFIX}/share/batch/service/batchd.sysv
check "User's Guide"			test -f ${PREFIX}/share/doc/BATCH/batch_users_guide.md
check "spool directory"			test -d ${SPOOL}/log
check "INSTALL.txt is not in a deb"	test ! -e ${PREFIX}/share/doc/BATCH/INSTALL.txt
for img in ${BATCH} ${BATCHD}
do
	out=$(ldd ${img} 2>&1)
	case "${out}" in
	*"not a dynamic executable"*)	ok "${img}: statically linked" ;;
	*)				bad "${img}: not static: ${out}" ;;
	esac
done

#  3.  Without systemd the postinst started it through /etc/init.d
if [ ! -d /run/systemd/system ]
then
	check "System V script is in /etc/init.d" test -x /etc/init.d/batchd
fi
wait_for "controller is running"	pidof batchd
wait_for "controller says its ident ${IDENT}" grep -q "%BATCH-S-IDENT.*(${IDENT})" ${SPOOL}/batchd.log
wait_for "controller listens"		test -S ${SPOOL}/batch.sock

#  4.
entry=$(submit "${JOBS}/who.sh" /NAME=plain /KEEP)
[ -n "${entry}" ] && ok "submit: entry ${entry}" || bad "submit: no entry number"
timeout 60 "${BATCH}" sync "${entry:-0}" >/dev/null 2>&1 && ok "sync: the job completed" || bad "sync: the job did not complete"
check "the log of the job says root"	grep -qx root ${JOBS}/plain_${entry}.log

#  5.  /USER and the owner go through getpwnam/getpwuid/initgroups of the glibc OF THIS HOST
entry=$(submit "${JOBS}/who.sh" /NAME=asuser /KEEP /RETAIN=ALWAYS /USER=nobody)
[ -n "${entry}" ] && ok "submit /USER=nobody: entry ${entry}" || bad "submit /USER=nobody: no entry number"
timeout 60 "${BATCH}" sync "${entry:-0}" >/dev/null 2>&1 && ok "sync: the job of nobody completed" || bad "sync: the job of nobody did not complete"
check "the job ran as nobody"		grep -qx nobody ${SPOOL}/log/asuser_${entry}.log
check "the log belongs to nobody"	test "$(stat -c %U ${SPOOL}/log/asuser_${entry}.log)" = nobody
check "SHOW /FULL names the owner"	sh -c "${BATCH} show ${entry} /full /all 2>&1 | grep -q 'Owner.*nobody'"

#  6.
check "HELP answers"			sh -c "${BATCH} help submit 2>&1 | grep -qi submit"

#  7.  A held job survives the restart a reinstall makes.
entry=$(submit "${JOBS}/who.sh" /NAME=held /HOLD)
old=$(pidof batchd)
apt-get install -y -q --reinstall "${deb}" >/dev/null 2>&1 && ok "apt reinstall" || bad "apt reinstall"
wait_for "controller is running again"	pidof batchd
[ "$(pidof batchd)" != "${old}" ] && ok "controller was restarted" || bad "controller was not restarted"
wait_for "the held job is still queued"	sh -c "${BATCH} show 2>&1 | grep -q held"

#  7a. An unknown user.  A static glibc image looks a name up through the NSS libraries it has to dlopen (libnss_systemd
#  of "passwd: files systemd") when the files do not have it: a controller that dies here is the defect this check
#  is here to find.  Last but one, so that a dead controller does not fail the checks of the steps before.
check "an unknown user is refused"	sh -c "${BATCH} submit ${JOBS}/who.sh /USER=nosuchperson 2>&1 | grep -q NOSUCHUSER"
check "the controller survived the unknown user" pidof batchd

#  8.
dpkg --remove batch >/dev/null 2>&1 && ok "dpkg --remove" || bad "dpkg --remove"
check "batch gone"			test ! -e ${BATCH}
check "batchd gone"			test ! -e ${BATCHD}
check "controller stopped"		test -z "$(pidof batchd)"
check "System V script gone"		test ! -e /etc/init.d/batchd
check "the spool stays"			test -f ${SPOOL}/batchd.log
dpkg --purge batch >/dev/null 2>&1 && ok "dpkg --purge" || bad "dpkg --purge"
check "the spool is gone"		test ! -e ${SPOOL}

[ ${FAILED} -eq 0 ] && echo "== all passed" || echo "== there are FAILED checks"
exit ${FAILED}
