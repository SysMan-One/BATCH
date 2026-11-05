#!/bin/bash
#++
#	FACILITY: BATCH - the kits for ARM, MIPS and x86
#
#	AUTHOR: StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
#
#	CREATION DATE: 04-NOV-2026
#
#	USAGE:
#		$ CROSS_PREFIX=<path>/bin/mipsel-buildroot-linux-gnu- \
#		  VHELP_SRC=<vHELP checkout> \
#		  packaging/build.sh mipsel [<output directory>]
#
#		<target> is one of: x86_64, armhf, arm64, mips, mipsel, mips64el
#
#	DESCRIPTION:
#		Builds BATCH (the utility "batch" and the job controller "batchd")
#		for ONE target processor and lays it out as installable kits in
#		the output directory (./dist by default):
#
#		    batch_<rev>-1_<arch>.deb	Debian, Ubuntu, Raspberry Pi OS
#		    batch-<rev>-1.<arch>.rpm	RHEL, Fedora, ALT, openSUSE
#		    BATCH-<ident>-<arch>.tar.gz	any Linux, by hand
#
#		There is no .ipk / .apk (OpenWrt): the queue journal lives in
#		/var/spool/batch, which is a tmpfs there, so the queue would not
#		survive a reboot - and it does not on any other system.
#
#		The images are linked STATICALLY, so one image of a processor
#		runs on every Linux of that processor whatever its C library -
#		the same file goes into all three kits.  One thing is NOT
#		independent of the host's C library: the user names.  /USER,
#		the owner shown by SHOW /FULL and the supplementary groups of
#		a job go through getpwnam/getpwuid/initgroups, which a static
#		glibc image answers through the NSS libraries OF THE HOST (and
#		only a glibc host has them).  KNOWN DEFECT of the static image:
#		a name the files do not have (/USER=nosuchperson) makes glibc
#		dlopen libnss_systemd of "passwd: files systemd" into a static
#		process, and batchd dies with SIGSEGV (seen on Debian 12; the
#		dynamic build answers NOSUCHUSER).  packaging/test/deb.sh checks
#		that on Debian 12/13 and Ubuntu 24.04 and FAILS until BATCH
#		looks the names up without NSS (/etc/passwd, /etc/group by
#		itself) or the images are linked dynamically.
#
#		The product is installed in /usr/local like "cmake --install"
#		does on the machines that build it from the source: /usr/bin/batch
#		is the command of the package "at" on Debian and Ubuntu, a
#		package of ours must not collide with it.
#
#		Everything is cross-built, x86_64 included, so that the help
#		libraries are ALWAYS translated by the VHELPTRAN of the build host.
#		The order is:
#
#		    1. StarLet and HELP for the build host	-> $WORK/host
#		       (once; only VHELPTRAN of it is used)
#		    2. StarLet and HELP for the target	-> $WORK/<target>/sysroot
#		    3. BATCH for the target, installed with DESTDIR into
#		       $WORK/<target>/stage
#		    4. the kits, out of the staged tree
#
#	ENVIRONMENT:
#		CROSS_PREFIX	prefix of the target tool chain, "gcc" is appended
#				(required)
#		VHELP_SRC	a checkout of the HELP package (required)
#		STARLET_TGZ	the StarLet kit, packaging/third_party/starlet-1.7.1.tar.gz
#				by default (its .sha256 is checked)
#		WORK		the build tree, ./build-pkg by default
#		PKG_RELEASE	the package release number, 1 by default
#		JOBS		parallel jobs of the build, nproc by default
#
#	MODIFICATION HISTORY:
#
#		04-NOV-2026	RRL	X-02-02ECO03 : Initial revision, after BroTwins' PACKAGING/BUILD.SH.
#
#--

set -euo pipefail

TOPDIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PKGDIR="${TOPDIR}/packaging"

TARGET="${1:-}"
DIST="$(mkdir -p "${2:-${TOPDIR}/dist}" && cd "${2:-${TOPDIR}/dist}" && pwd)"

: "${CROSS_PREFIX:?CROSS_PREFIX names the target tool chain, e.g. .../bin/mipsel-buildroot-linux-gnu-}"
: "${VHELP_SRC:?VHELP_SRC names a checkout of the HELP package}"

STARLET_TGZ="${STARLET_TGZ:-${PKGDIR}/third_party/starlet-1.7.1.tar.gz}"
WORK="${WORK:-${TOPDIR}/build-pkg}"
PKG_RELEASE="${PKG_RELEASE:-1}"
JOBS="${JOBS:-$(nproc)}"

#
#	The processor as CMake names it (it goes into the CMake cache), and the
#	name of the same processor in each package format.
#
case "${TARGET}" in
	x86_64)	PROC=x86_64;	DEB_ARCH=amd64;		RPM_ARCH=x86_64		;;
	armhf)	PROC=armv7l;	DEB_ARCH=armhf;		RPM_ARCH=armv7hl	;;
	arm64)	PROC=aarch64;	DEB_ARCH=arm64;		RPM_ARCH=aarch64	;;
	mips)	PROC=mips;	DEB_ARCH=mips;		RPM_ARCH=mips		;;
	mipsel)	PROC=mipsel;	DEB_ARCH=mipsel;	RPM_ARCH=mipsel		;;
	mips64el)	PROC=mips64;	DEB_ARCH=mips64el;	RPM_ARCH=mips64el	;;
	*)	echo "Usage: $0 {x86_64|armhf|arm64|mips|mipsel|mips64el} [<output directory>]" >&2
		exit 2 ;;
esac

CC_TARGET="${CROSS_PREFIX}gcc"
STRIP_TARGET="${CROSS_PREFIX}strip"

command -v "${CC_TARGET}" >/dev/null || { echo "$0: ${CC_TARGET} is not there" >&2; exit 2; }

HOST="${WORK}/host"
TDIR="${WORK}/${TARGET}"
SYSROOT="${TDIR}/sysroot"
STAGE="${TDIR}/stage"
HOST_TRAN="${HOST}/bin/vhelptran"
HOST_HELP="${HOST}/bin/vhelp"

#	Where it lands on the target.  The same value goes into the
#	CMake cache, because the help library path is baked into the image.
PREFIX=/usr/local
SPOOL=/var/spool/batch
SERVICEDIR=${PREFIX}/share/batch/service
DOCDIR=${PREFIX}/share/doc/BATCH

say ()
{
	echo "-----> $*"
}

#
#	The ident of the product is BATCH_K_IDENT of the top CMakeLists.txt - the
#	very value that the controller prints in its banner "%BATCH-S-IDENT".  The
#	head module BATCHD.C must say the same, the release job checks it against
#	the tag.  The revision (X-aa-bbECOcc -> aa.bb.cc) is what the packages
#	are versioned by, and BATCHD.C carries it as __REV__.
#
IDENT=$(sed -n 's/^set(BATCH_K_IDENT[[:space:]]*"\([^"]*\)".*/\1/p' "${TOPDIR}/CMakeLists.txt")
[[ "${IDENT}" =~ ^X-([0-9]+)-([0-9]+)(ECO([0-9]+))?$ ]] || { echo "$0: BATCH_K_IDENT '${IDENT}' is not X-aa-bb[ECOcc]" >&2; exit 2; }
REV=$(printf '%02d.%02d.%02d' "$((10#${BASH_REMATCH[1]}))" "$((10#${BASH_REMATCH[2]}))" "$((10#${BASH_REMATCH[4]:-0}))")

HEAD_IDENT=$(sed -n 's/^#define[[:space:]]*__IDENT__[[:space:]]*"\(.*\)".*/\1/p' "${TOPDIR}/src/batchd.c")
HEAD_REV=$(sed -n 's/^#define[[:space:]]*__REV__[[:space:]]*"\(.*\)".*/\1/p' "${TOPDIR}/src/batchd.c")

[ "${HEAD_IDENT}" = "${IDENT}" ] && [ "${HEAD_REV}" = "${REV}" ] \
	|| { echo "$0: CMakeLists.txt says ${IDENT} (${REV}), src/batchd.c says ${HEAD_IDENT} (${HEAD_REV})" >&2; exit 2; }

say "BATCH ${IDENT}, rev ${REV}, ${TARGET}"

#
#	The StarLet kit is unpacked once, beside the build trees.
#
if [ -f "${STARLET_TGZ}.sha256" ]
then
	(cd "$(dirname "${STARLET_TGZ}")" && sha256sum -c --quiet "$(basename "${STARLET_TGZ}").sha256")
fi

STARLET_SRC="${WORK}/src/$(basename "${STARLET_TGZ}" .tar.gz)"

if [ ! -f "${STARLET_SRC}/CMakeLists.txt" ]
then
	mkdir -p "${WORK}/src"
	tar -xzf "${STARLET_TGZ}" -C "${WORK}/src"
fi

HELP_OPTS=(-DBUILD_TESTING=OFF -DHELP_TESTS=OFF -DHELP_BUILD_SYSCACHE=OFF -DHELP_ALIAS=OFF)

#
#	1. The build host: VHELPTRAN translates the help libraries of every target.
#
if [ ! -x "${HOST_TRAN}" ]
then
	say "StarLet and HELP for the build host"

	cmake -S "${STARLET_SRC}" -B "${HOST}/build/starlet" -DCMAKE_BUILD_TYPE=Release \
		-DBUILD_TESTING=OFF -DCMAKE_INSTALL_PREFIX="${HOST}"
	cmake --build "${HOST}/build/starlet" -j "${JOBS}"
	cmake --install "${HOST}/build/starlet"

	cmake -S "${VHELP_SRC}" -B "${HOST}/build/help" -DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_PREFIX_PATH="${HOST}" -DCMAKE_INSTALL_PREFIX="${HOST}" "${HELP_OPTS[@]}"
	cmake --build "${HOST}/build/help" -j "${JOBS}"
	cmake --install "${HOST}/build/help"
fi

#
#	2. The libraries of the target.  CMAKE_SYSTEM_NAME makes it a cross build
#	whatever the processor, x86_64 included.
#
CROSS=(	-DCMAKE_SYSTEM_NAME=Linux
	-DCMAKE_SYSTEM_PROCESSOR="${PROC}"
	-DCMAKE_C_COMPILER="${CC_TARGET}"
	-DCMAKE_BUILD_TYPE=Release
	-DCMAKE_PREFIX_PATH="${SYSROOT}"
	-DHELP_HOST_TRANSLATOR="${HOST_TRAN}" )

say "StarLet and HELP for ${TARGET} (${PROC})"
rm -rf "${TDIR}"

cmake -S "${STARLET_SRC}" -B "${TDIR}/build/starlet" "${CROSS[@]}" \
	-DBUILD_TESTING=OFF -DCMAKE_INSTALL_PREFIX="${SYSROOT}"
cmake --build "${TDIR}/build/starlet" -j "${JOBS}"
cmake --install "${TDIR}/build/starlet"

cmake -S "${VHELP_SRC}" -B "${TDIR}/build/help" "${CROSS[@]}" \
	-DCMAKE_INSTALL_PREFIX="${SYSROOT}" "${HELP_OPTS[@]}"
cmake --build "${TDIR}/build/help" -j "${JOBS}"
cmake --install "${TDIR}/build/help"

#
#	3. BATCH.  What the top CMakeLists.txt would find by itself is told
#	explicitly, because it would find the wrong thing:
#
#	  BATCH_K_HELPTRAN / BATCH_K_HELPEXE	are looked for beside the HELP package
#			that was found - the one of the TARGET, a foreign program.
#			The host's VHELPTRAN translates the help libraries; VHELP is
#			named for the install script only, which a staged installation
#			skips.
#	  BATCH_M_SYSVINIT	is OFF by default on a systemd host, the kits carry
#			the System V script too, in a directory of its own.
#	  BATCH_M_AUTOSTART	OFF: the maintainer scripts do what a staged installation
#			leaves to the package.
#	  BATCH_M_TESTS	OFF: ctest is run on the build host, not here.
#
say "BATCH for ${TARGET}"

cmake -S "${TOPDIR}" -B "${TDIR}/build/batch" "${CROSS[@]}" \
	-DCMAKE_EXE_LINKER_FLAGS="-static" \
	-DCMAKE_INSTALL_PREFIX="${PREFIX}" \
	-DBATCH_K_SPOOLROOT="${SPOOL}" \
	-DBATCH_K_HELPTRAN="${HOST_TRAN}" \
	-DBATCH_K_HELPEXE="${HOST_HELP}" \
	-DBATCH_M_TESTS=OFF \
	-DBATCH_M_SYSTEMD=ON \
	-DBATCH_K_SYSTEMDDIR=/usr/lib/systemd/system \
	-DBATCH_M_SYSVINIT=ON \
	-DBATCH_K_INITDIR="${SERVICEDIR}" \
	-DBATCH_M_AUTOSTART=OFF >"${TDIR}/build/batch.cfg.log" 2>&1 \
	|| { cat "${TDIR}/build/batch.cfg.log"; exit 1; }

cmake --build "${TDIR}/build/batch" -j "${JOBS}"
DESTDIR="${STAGE}" cmake --install "${TDIR}/build/batch"

"${STRIP_TARGET}" "${STAGE}${PREFIX}/bin/batch" "${STAGE}${PREFIX}/sbin/batchd"

#	The start script of a system without systemd travels in every kit, but
#	not as /etc/init.d/batchd - the postinst of a package puts it there.
mv "${STAGE}${SERVICEDIR}/batchd" "${STAGE}${SERVICEDIR}/batchd.sysv"

#
#	A maintainer script out of its template: @NAME@ is the package, @PREFIX@
#	the prefix, @SERVICEDIR@ where the System V script is.
#
script ()
{
	sed -e "s|@NAME@|batch|g" -e "s|@PREFIX@|${PREFIX}|g" -e "s|@SERVICEDIR@|${SERVICEDIR}|g" \
		-e "s|@SPOOL@|${SPOOL}|g" "${PKGDIR}/scripts/$1.in"
}

#	What the package is: one line, and the paragraph under it.
SUMMARY="BATCH - batch execution subsystem for shell scripts, like the OpenVMS batch queue"
BLURB="The utility batch and the job controller batchd, statically linked, help libraries
 (English, Russian, French), the manuals, the systemd unit and the System V script."

#
#	BATCH-<ident>-<target>.tar.gz: the staged tree, to be unpacked over /
#
make_tgz ()
{
local	kit="BATCH-${IDENT}-${TARGET}"

	install -m 0644 "${PKGDIR}/INSTALL.txt" "${STAGE}${DOCDIR}/INSTALL.txt"
	tar --owner=0 --group=0 -czf "${DIST}/${kit}.tar.gz" -C "${STAGE}" $(ls "${STAGE}")
	rm -f "${STAGE}${DOCDIR}/INSTALL.txt"
	say "${DIST}/${kit}.tar.gz"
}

make_deb ()
{
local	root="${TDIR}/deb" ver="${REV}-${PKG_RELEASE}"

	rm -rf "${root}"
	cp -a "${STAGE}" "${root}"
	mkdir -p "${root}/DEBIAN"

	cat >"${root}/DEBIAN/control" <<-EOF
	Package: batch
	Version: ${ver}
	Architecture: ${DEB_ARCH}
	Maintainer: StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan) <zator@yandex.ru>
	Section: admin
	Priority: optional
	Homepage: https://github.com/SysMan-One/BATCH
	Installed-Size: $(du -sk "${STAGE}" | cut -f1)
	Description: ${SUMMARY}
	 ${BLURB}
	 The queue and its journal are in ${SPOOL}; the service is enabled and
	 started by the package.
	EOF

	for s in postinst prerm postrm
	do
		script "${s}" >"${root}/DEBIAN/${s}"
		chmod 0755 "${root}/DEBIAN/${s}"
	done

	dpkg-deb --root-owner-group -Zxz --build "${root}" "${DIST}/batch_${ver}_${DEB_ARCH}.deb" >/dev/null
	say "${DIST}/batch_${ver}_${DEB_ARCH}.deb"
}

make_rpm ()
{
local	top="${TDIR}/rpm" spec="${TDIR}/rpm/batch.spec"

	rm -rf "${top}"
	mkdir -p "${top}"

	#  The image is foreign to the build host: no strip, no debuginfo, no
	#  dependency scan by the tools of the build host.
	{
	cat <<-EOF
	%define _build_id_links none
	%define debug_package %{nil}
	%define __os_install_post %{nil}
	%define _binaries_in_noarch_packages_terminate_build 0
	Name:		batch
	Version:	${REV}
	Release:	${PKG_RELEASE}
	Summary:	${SUMMARY}
	License:	Apache-2.0
	URL:		https://github.com/SysMan-One/BATCH
	AutoReqProv:	no

	%description
	${BLURB//$'\n'/ }

	%install
	cp -a "${STAGE}/." "%{buildroot}/"

	%files
	EOF

	(cd "${STAGE}" && find . -type f -print -o -type l -print) | sed 's|^\.||'
	echo "%dir ${PREFIX}/share/batch"
	echo "%dir ${SERVICEDIR}"
	echo "%dir ${DOCDIR}"
	echo "%dir ${SPOOL}"
	echo "%dir ${SPOOL}/log"

	echo "%post"
	script postinst | sed '1d'
	echo "%preun"
	script prerm | sed '1d'
	echo "%postun"
	script postrm | sed '1d'
	} >"${spec}"

	rpmbuild -bb --quiet --target "${RPM_ARCH}-linux" \
		--define "_topdir ${top}" --define "_rpmdir ${top}/out" "${spec}" >/dev/null 2>"${top}/rpmbuild.log" \
		|| { cat "${top}/rpmbuild.log"; return 1; }

	find "${top}/out" -name '*.rpm' -exec cp {} "${DIST}/" \; -exec sh -c 'echo "-----> '"${DIST}"'/$(basename "$1")"' _ {} \;
}

#	4.
make_tgz
make_deb
make_rpm

say "Done: ${TARGET}"
