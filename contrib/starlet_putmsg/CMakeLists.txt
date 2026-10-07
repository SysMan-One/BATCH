#++
#  MODULE:		contrib/starlet_putmsg/CMakeLists.txt
#  IDENT:		X-01
#  REV:			1.0.01
#
#  FACILITY:		STARLET
#
#  ABSTRACT:		Build of the demonstration of __UTIL$PUTMSG_FAO / __UTIL$PUTMSGD_FAO. The pair
#			itself now lives in UTILITY_ROUTINES.C beside the existing one.
#			The directory is not part of the BATCH build; it is configured on its own:
#
#			  cmake -S contrib/starlet_putmsg -B build-putmsg
#			  cmake --build build-putmsg && ./build-putmsg/starletmsg_demo
#
#  AUTHOR:		StarLet squad and Ruslan R. Laishev
#
#  CREATION DATE:	28-JUL-2026
#
#  MODIFICATION HISTORY:
#
#	28-JUL-2026	X-01	Initial revision.
#--
#

cmake_minimum_required(VERSION 3.16)

project(STARLETMSG VERSION 1.0.0 DESCRIPTION "Message issuing on the FAO engine" LANGUAGES C)

set(STARLETMSG$K_STARLETDIR "${CMAKE_CURRENT_SOURCE_DIR}/../../3rdparty/utility_routines"
					CACHE PATH	"Location of the UTILITY$ROUTINES subproject")

add_executable(starletmsg_demo
	demo.c
	"${STARLETMSG$K_STARLETDIR}/starlet.c"
	"${STARLETMSG$K_STARLETDIR}/utility_routines.c")

target_include_directories(starletmsg_demo PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}"
	"${STARLETMSG$K_STARLETDIR}")

target_compile_definitions(starletmsg_demo PRIVATE _GNU_SOURCE=1)
target_compile_features(starletmsg_demo PRIVATE c_std_11)
set_target_properties(starletmsg_demo PROPERTIES C_EXTENSIONS ON)

#  The vendored modules are not held to the diagnostic set of this proposal
set_source_files_properties(
	"${STARLETMSG$K_STARLETDIR}/starlet.c"
	"${STARLETMSG$K_STARLETDIR}/utility_routines.c"
	PROPERTIES COMPILE_OPTIONS "-w")

target_compile_options(starletmsg_demo PRIVATE -Wall -Wextra -Wno-unused-parameter)
