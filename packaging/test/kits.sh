#!/bin/bash
#++
#	FACILITY: BATCH - the tests of the kits
#
#	USAGE:
#		$ packaging/test/kits.sh <ident> <directory with the kits> <target>
#
#		<target> is one of: x86_64, armhf, arm64, mips, mipsel, mips64el
#
#	ENVIRONMENT:
#		NORUN=1		do not start the images (no qemu-user for the target here)
#
#	DESCRIPTION: The kits of ONE processor, any processor, on a build host
#		(Ubuntu, a user is enough): what is in them and that the images
#		are of the right processor.  The images of a foreign processor
#		start under qemu-user, which the kernel calls through binfmt_misc
#		(package qemu-user-static) - the images are static, no root
#		file system of the target is needed.
#
#		    1. the .tar.gz holds both images; readelf says the ELF class,
#		       the byte order and the machine of <target>, and that they
#		       are static
#		    2. the controller starts on a private root directory, prints
#		       %BATCH-S-IDENT with <ident>, and a job goes through
#		       (SUBMIT, SYNC, its log); the job is run by the /bin/sh of
#		       the build host - the one thing that is not of <target>
#		    3. the .deb says the Architecture of <target>, the .rpm the
#		       one of <target>, and both hold the same images as the
#		       .tar.gz does (same sha256)
#
#		The install into a live system and /USER (NSS of the host's
#		glibc) are the business of deb.sh.
#
#	AUTHOR: StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
#
#	CREATION DATE: 04-NOV-2026
#
#	MODIFICATION HISTORY:
#
#		04-NOV-2026	RRL	X-02-02ECO03 : Initial revision, after BroTwins' PACKAGING/TEST/KITS.SH.
#--

set -u

IDENT="${1:?the ident of the tag}"
DIR="$(cd "${2:?the directory of the kits}" && pwd)"
TARGET="${3:?the target}"
FAILED=0

ok ()	{ echo "ok    : $*"; }
bad ()	{ echo "FAILED: $*"; FAILED=1; }
check ()
{
local	what="$1"
	shift
	if "$@" >/dev/null 2>&1; then ok "${what}"; else bad "${what}"; fi
}

#	The ELF header as readelf prints it: class, data, machine
case "${TARGET}" in
	x86_64)		ELF="ELF64|little endian|X86-64";	DEB=amd64;	RPM=x86_64 ;;
	armhf)		ELF="ELF32|little endian|ARM";		DEB=armhf;	RPM=armv7hl ;;
	arm64)		ELF="ELF64|little endian|AArch64";	DEB=arm64;	RPM=aarch64 ;;
	mips)		ELF="ELF32|big endian|MIPS";		DEB=mips;	RPM=mips ;;
	mipsel)		ELF="ELF32|little endian|MIPS";		DEB=mipsel;	RPM=mipsel ;;
	mips64el)	ELF="ELF64|little endian|MIPS";		DEB=mips64el;	RPM=mips64el ;;
	*)		echo "$0: unknown target ${TARGET}" >&2; exit 2 ;;
esac

TMP=$(mktemp -d)
CTL=0
trap '[ ${CTL} -ne 0 ] && kill ${CTL} 2>/dev/null; rm -rf "${TMP}"' EXIT

echo "== ${TARGET}, tag ${IDENT}, kits in ${DIR}"

#  1.
tgz="${DIR}/BATCH-${IDENT}-${TARGET}.tar.gz"
[ -f "${tgz}" ] || { bad "no ${tgz}"; exit 1; }
mkdir -p "${TMP}/tgz"
tar -xzf "${tgz}" -C "${TMP}/tgz"

check "tgz holds INSTALL.txt"		test -f "${TMP}/tgz/usr/local/share/doc/BATCH/INSTALL.txt"
check "tgz holds the System V script"	test -x "${TMP}/tgz/usr/local/share/batch/service/batchd.sysv"
check "tgz holds the systemd unit"	test -f "${TMP}/tgz/usr/lib/systemd/system/batchd.service"
check "tgz holds the spool directory"	test -d "${TMP}/tgz/var/spool/batch/log"

for img in usr/local/bin/batch usr/local/sbin/batchd
do
	check "tgz holds ${img}"	test -x "${TMP}/tgz/${img}"

	hdr=$(readelf -h "${TMP}/tgz/${img}" 2>&1)
	IFS='|' read -r cls dat mach <<<"${ELF}"
	if echo "${hdr}" | grep -q "Class:.*${cls}" && echo "${hdr}" | grep -qi "Data:.*${dat}" && echo "${hdr}" | grep -q "Machine:.*${mach}"
	then ok "${img}: ELF is ${ELF}"; else bad "${img}: ELF is not ${ELF}"; echo "${hdr}" | grep -E 'Class|Data|Machine'; fi
	check "${img}: static (no INTERP)"	test -z "$(readelf -lW "${TMP}/tgz/${img}" | grep INTERP)"
done

#  2.  The controller on a root of its own; the images are the ones of <target>, qemu-user runs them.
#  The root is private to this user, and so is the job: no /USER here.
if [ -z "${NORUN:-}" ]
then
	B="${TMP}/tgz/usr/local/bin/batch"
	D="${TMP}/tgz/usr/local/sbin/batchd"
	ROOT="${TMP}/root"
	mkdir -p "${ROOT}"
	printf '#!/bin/sh\necho done-by-the-job\n' >"${TMP}/job.sh"
	chmod 755 "${TMP}/job.sh"

	"${D}" /FOREGROUND "/ROOT=${ROOT}" >"${ROOT}/batchd.out" 2>&1 &
	CTL=$!

	n=0
	while [ ${n} -lt 60 ] && [ ! -S "${ROOT}/batch.sock" ]
	do
		n=$((n + 1)); sleep 0.5
	done
	head -3 "${ROOT}/batchd.out"
	check "the controller listens"		test -S "${ROOT}/batch.sock"
	check "the banner carries the ident ${IDENT}"	grep -q "%BATCH-S-IDENT.*(${IDENT})" "${ROOT}/batchd.out"

	export BATCH_ROOT="${ROOT}"
	out=$(cd "${TMP}" && timeout 60 "${B}" submit "${TMP}/job.sh" /NAME=rt /KEEP 2>&1)
	echo "${out}"
	entry=$(echo "${out}" | sed -n 's/.*entry \([0-9][0-9]*\).*/\1/p' | head -1)
	[ -n "${entry}" ] && ok "submit: entry ${entry}" || bad "submit: no entry number"

	timeout 120 "${B}" sync "${entry:-0}" >/dev/null 2>&1 && ok "sync: the job completed" || bad "sync: the job did not complete"
	check "the log of the job holds its output"	grep -q done-by-the-job "${TMP}/rt_${entry}.log"
	check "SHOW answers"			timeout 60 "${B}" show /all

	kill "${CTL}" 2>/dev/null; wait "${CTL}" 2>/dev/null; CTL=0
fi

sum_b=$(sha256sum "${TMP}/tgz/usr/local/bin/batch" | cut -d' ' -f1)
sum_d=$(sha256sum "${TMP}/tgz/usr/local/sbin/batchd" | cut -d' ' -f1)

#  3.
deb=$(ls "${DIR}"/batch_*_"${DEB}".deb 2>/dev/null | head -1)
if [ -n "${deb}" ]
then
	[ "$(dpkg-deb -f "${deb}" Architecture)" = "${DEB}" ] && ok "deb is ${DEB}" || bad "deb is not ${DEB}"
	mkdir -p "${TMP}/deb"; dpkg-deb -x "${deb}" "${TMP}/deb"
	[ "$(sha256sum "${TMP}/deb/usr/local/bin/batch" | cut -d' ' -f1)" = "${sum_b}" ] \
		&& [ "$(sha256sum "${TMP}/deb/usr/local/sbin/batchd" | cut -d' ' -f1)" = "${sum_d}" ] \
		&& ok "deb holds the images of the tgz" || bad "deb images differ"
else bad "no .deb for ${DEB}"; fi

rpm=$(ls "${DIR}"/batch-*."${RPM}".rpm 2>/dev/null | head -1)
if [ -n "${rpm}" ]
then
	[ "$(rpm -qp --qf '%{ARCH}' "${rpm}" 2>/dev/null)" = "${RPM}" ] && ok "rpm is ${RPM}" || bad "rpm is not ${RPM}"
	mkdir -p "${TMP}/rpm"; (cd "${TMP}/rpm" && rpm2cpio "${rpm}" | cpio -idm --quiet 2>/dev/null)
	[ "$(sha256sum "${TMP}/rpm/usr/local/bin/batch" | cut -d' ' -f1)" = "${sum_b}" ] \
		&& [ "$(sha256sum "${TMP}/rpm/usr/local/sbin/batchd" | cut -d' ' -f1)" = "${sum_d}" ] \
		&& ok "rpm holds the images of the tgz" || bad "rpm images differ"
else bad "no .rpm for ${RPM}"; fi

[ ${FAILED} -eq 0 ] && echo "== all passed" || echo "== there are FAILED checks"
exit ${FAILED}
