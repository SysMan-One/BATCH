#++
#  MODULE:		CMakeLists.txt (top level)
#  IDENT:		X-01-29
#  REV:			01.29.00
#
#  FACILITY:		BATCH - batch execution subsystem for shell scripts, a functional equivalent of
#			the OpenVMS batch job queue (SUBMIT / SHOW QUEUE).
#
#  ABSTRACT:		Top-level build file. It establishes the product identification and the common
#			compilation options, generates the header BATCHVERS.H and includes the SRC and
#			TEST directories.
#
#			Naming convention: constants and build indicators are named in the VMS style
#			with the dollar separator - BATCH_K_* for constants, BATCH_M_* for indicators.
#			The exception is the variables prefixed BATCH_CFG_ that feed configure_file:
#			substitution of the form @NAME@ does not recognize a dollar sign within a
#			variable name, so separate names without the separator were introduced for the
#			template BATCHVERS.H.IN.
#
#  AUTHOR:		StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
#
#  CREATION DATE:	28-JUL-2026
#
#  MODIFICATION HISTORY:
#
#	28-JUL-2026	RRL	X-01-01	Initial revision. The build was converted from Makefile to CMake. The
#				object library BATCHCOM, the generated header BATCHVERS.H and the
#				functional test SMOKE were introduced.
#
#	28-JUL-2026	RRL	X-01-02	The INCLUDE directory was abolished: sources and headers now reside
#				together in SRC, and everything pertaining to verification in TEST. The
#				generated header is placed in SRC of the build tree.
#
#	28-JUL-2026	RRL	X-01-03	Comments converted to English.
#
#	28-JUL-2026	RRL	X-01-04	The vendored UTILITY$ROUTINES subproject is included and its target
#				STARLET is linked into every image. The subsystem operates in UTC, so
#				the STARLET time services are usable without a timezone conversion.
#
#	29-JUL-2026	RRL	X-01-05	Installation completed: the systemd unit of the controller, the guide and
#				the README are put in place, and an UNINSTALL target removes what was
#				installed, reading the manifest rather than guessing.
#
#	29-JUL-2026	RRL	X-01-06	BATCH_M_PDF renders the manuals as PDF and installs them alongside the
#				markdown they are made from.
#
#	29-JUL-2026	RRL	X-01-07	The manuals exist in Russian as well and are rendered alongside.
#
#	30-JUL-2026	RRL	X-01-08	Target DOC_PACK gathers the manuals - both guides, both languages, both
#				formats - into one archive.
#
#	30-JUL-2026	RRL	X-01-09	The rendered manuals belong to the sources: they are made beside the
#				markdown, kept under version control and installed unconditionally. The
#				DOC_PACK target was withdrawn - there is one set to hand over, not two,
#				and BATCH_M_PDF now says only whether the PDF is to be made afresh.
#
#	01-AUG-2026	RRL	X-01-10	The regression tests the subproject now carries are governed by
#				BATCH_M_TESTS along with ours.
#
#	 1-AUG-2026	RRL	X-01-11	An installed UTILITY$ROUTINES is preferred to the copy carried in
#				3rdparty, which stays for a system that has no package. BATCH_M_VENDORED
#				forces the carried copy.
#
#	 1-AUG-2026	RRL	X-01-12	The carried copy is gone: STARLET and HELP are required packages and
#				the tree describes them as nothing else. Two revisions of one library in
#				one image cannot arise where there is one library.
#
#	 2-AUG-2026	RRL	X-01-11	Target DIST assembles the kit and names it for the facility, the version
#				and the revision it carries. BATCH_K_IDENT was raised to X-02: it had stood
#				at X-01 while the modules went to X-28, so it said nothing at all.
#
#	 3-AUG-2026	RRL	X-01-12	The kit is named BATCH-<ident>.tar, after the kits of the neighbouring
#				products, and is not compressed - a kit read the same way as its neighbours
#				is one less thing to explain.
#
#	 3-AUG-2026	RRL	X-01-13	The images of the HELP facility are looked for beside the package that was
#				found before they are looked for along the path: an installation into
#				/usr/local is often met with /usr/local/bin absent from the path, and the
#				kit then reported HELPTRAN missing while the package it belongs to was in
#				hand.
#
#	 3-AUG-2026	RRL	X-01-14	Both spellings of the images are looked for, and the VMS one first: the
#				HELP facility names them VHELP and VHELPTRAN, "help" being a builtin of the
#				Bourne-again shell which no ordering of the path can get past.
#
#	 4-AUG-2026	RRL	X-01-15	UTILITY$ROUTINES is linked shared where the package offers a shared
#				library. A correction to it then reaches these images when the library is
#				replaced, and several products of the family hold one copy between them.
#				BATCH_M_STATICLIB binds it in instead.
#
#	 7-AUG-2026	RRL	X-01-16	The product revision is X-03. It stood at X-02 while queues of their own,
#				SHOW/QUEUES, the QUEUE verb, the GNU form of a qualifier and a third
#				language of the manuals were added: a revision that does not move says
#				nothing, and it is the only thing the name of a kit tells the world.
#
#	 7-AUG-2026	RRL	X-01-17	UTILITY$ROUTINES is bound into the images by default. The SOVERSION of the
#				shared library has stood at 1 while the layouts of its structures moved, and
#				this subsystem embeds those layouts: bound in, a mismatch is caught by the
#				linker; shared, it is caught by nothing.
#
#	 7-AUG-2026	RRL	X-01-18	The kit is <name>-<ident>.tar.gz. It had been left uncompressed, which
#					differed from the convention for no reason worth the difference.
#
#	20-AUG-2026	RRL	X-01-19	The identification is X-aa-bb, as it is in the neighbouring products,
#					and REV is derived from it part for part: X-01-29 gives 01.29.00, the third
#					place being the ECO number of a small correction - X-01-29ECO01 gives 01.29.01.
#
#	20-AUG-2026	RRL	X-01-20	The product revision is X-01-03, in the same form as the modules carry:
#					one form of identification for the whole product, and the name of the kit
#					says it - BATCH-X-01-03.tar.gz.
#
#	21-AUG-2026	RRL	X-01-21	The product revision is X-01-04: the PURGE command, /CONFIRM before an
#					irrevocable act, and the dump of ANALYZE telling how a job ended.
#
#	21-AUG-2026	RRL	X-01-22	The product revision is X-01-05: the User's Guide exists in French, and
#					the manuals are now four in three languages.
#
#	21-AUG-2026	RRL	X-01-23	The product revision is X-01-06: the three User's Guides say the same
#					things. The English and the Russian had fallen a week behind the French -
#					PURGE, /CONFIRM, SYNC, /USER, /QUEUES and /MANAGER were in none of them,
#					and the English still spoke of a vendored subproject.
#
#	31-AUG-2026	RRL	X-01-24	The product revision is X-01-07: $PUTMSG_FAO throughout, the three help
#					libraries and the three manuals brought into step, and two checks that
#					keep them so.
#
#	31-AUG-2026	RRL	X-01-25	No identifier of a CMake file carries a dollar. CMake is not the C
#					preprocessor: a dollar within a name does not belong to it there, and
#					inside a generated script it costs a silent empty expansion rather than
#					a refusal. The C macros keep theirs - that is the convention of the
#					sources, and the two languages are not the same language.
#
#	 8-SEP-2026	RRL	X-01-26	BATCH_K_IDENT raised to X-01-08: the SET ENTRY command.
#
#	12-SEP-2026	RRL	X-01-27	BATCH_K_IDENT raised to X-01-09: batch.hlb is rooted at the facility key
#				BATCH rather than spilling its verbs into the shared catalogue's top level.
#
#	 7-OCT-2026	RRL	X-01-28	BATCH_K_IDENT raised to X-01-10: the HELP command looks for its topics
#				under the facility key the library is rooted at (BATCH.C X-01-35).
#
#	 8-OCT-2026	RRL	X-01-29	BATCH_K_IDENT raised to X-01-11: SHOW without /ALL displays the jobs that
#				wait, and /ALL adds the entries kept after a success (BATCHD.C X-01-32).
#--
#

cmake_minimum_required(VERSION 3.16)

project(BATCH
	VERSION		1.0.1
	DESCRIPTION	"Batch execution subsystem for shell scripts, an equivalent of the OpenVMS batch job queue"
	LANGUAGES	C)

#
#  Product identification. BATCH_K_IDENT corresponds to the IDENT field in the source module headers.
#
set(BATCH_K_FACILITY	"BATCH"		CACHE STRING	"Facility name substituted into $LOG messages")
set(BATCH_K_IDENT	"X-01-11"	CACHE STRING	"Product revision identification")
set(BATCH_K_SPOOLROOT	"/var/spool/batch"
					CACHE PATH	"Default subsystem root directory")
set(BATCH_K_SHELL	"/bin/sh"	CACHE FILEPATH	"Command interpreter created for a job")

option(BATCH_M_STRICT	"Treat compiler diagnostics as errors"				OFF)
option(BATCH_M_TESTS	"Build and register the functional tests"			ON)
option(BATCH_M_SYSTEMD	"Install the systemd unit of the job controller"		ON)
option(BATCH_M_AUTOSTART	"Enable and start the job controller at installation"		ON)
option(BATCH_M_PDF	"Re-render the manuals as PDF from the markdown"			OFF)

set(BATCH_K_SYSTEMDDIR	"/lib/systemd/system"
					CACHE PATH	"Where the systemd unit is installed")

#
#  Default build type. An empty CMAKE_BUILD_TYPE in a single-configuration generator means
#  compilation without optimization and without debugging information, which is undesirable.
#
if ( NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES )
	set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "Build type" FORCE)
	set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif()

#
#  The subsystem relies on AF_UNIX, SO_PEERCRED, daemon(3) and poll(2). Porting to other platforms
#  calls for separate work, so a build outside Linux is terminated during configuration.
#
if ( NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" )
	message(FATAL_ERROR "Facility ${BATCH_K_FACILITY} is implemented for Linux; this platform is ${CMAKE_SYSTEM_NAME}")
endif()

#
#  Build identifier: the configuration date and, where available, the short revision hash.
#
string(TIMESTAMP BATCH_K_BUILDDATE "%d-%m-%Y %H:%M:%S" UTC)
set(BATCH_K_BUILDREV "no-vcs")

find_package(Git QUIET)

if ( BATCH_M_PDF )
	find_package(Python3 COMPONENTS Interpreter REQUIRED)
endif()

if ( GIT_FOUND AND EXISTS "${PROJECT_SOURCE_DIR}/.git" )
	execute_process(COMMAND		"${GIT_EXECUTABLE}" rev-parse --short HEAD
			WORKING_DIRECTORY	"${PROJECT_SOURCE_DIR}"
			OUTPUT_VARIABLE		BATCH_K_BUILDREV
			OUTPUT_STRIP_TRAILING_WHITESPACE
			ERROR_QUIET)
endif()

#
#  Generation of the header BATCHVERS.H. Names without the dollar separator are unavoidable here,
#  see ABSTRACT.
#
set(BATCH_CFG_FACILITY	"${BATCH_K_FACILITY}")
set(BATCH_CFG_IDENT	"${BATCH_K_IDENT}")
set(BATCH_CFG_VERSION	"${PROJECT_VERSION}")
set(BATCH_CFG_SPOOLROOT	"${BATCH_K_SPOOLROOT}")
set(BATCH_CFG_SHELL	"${BATCH_K_SHELL}")
set(BATCH_CFG_BUILDID	"${BATCH_K_BUILDDATE} ${BATCH_K_BUILDREV}")

#
#  The destinations are wanted before the header is generated: the specification of the help library
#  is one of the values substituted into it, and a path computed from a variable not yet set comes
#  out empty - which the utility discovers only when someone asks for help.
#
include(GNUInstallDirs)

#
#  The HELP image, wanted at installation and at removal to rebuild the catalogue it shares between
#  its users. It is looked for here rather than beside HELPTRAN below, the value being wanted before
#  the templates are filled in.
#
#
#  The images of the HELP facility are looked for beside the package that was found, and only then
#  along the path. A package found at <prefix>/lib/cmake/HELP has its images at <prefix>/bin, and
#  that is the pair that belongs together: relying upon the path finds nothing where <prefix>/bin is
#  not on it - which is how an installation into /usr/local is often met - and would find the images
#  of another installation where two exist.
#
get_filename_component(BATCH_K_HELPROOT "${HELP_DIR}/../../.." ABSOLUTE)

#
#  Both spellings are looked for, and the VMS one first. The HELP facility names its images VHELP and
#  VHELPTRAN by default: "help" is a builtin of the Bourne-again shell and of its kin, and a shell
#  looks a builtin up before the search path, so no ordering of the path can bring an image of that
#  name to the front. An installation that took the older names is met all the same - HELP$K_HELPNAME
#  is a setting of that product, and a site may have set it.
#
find_program(BATCH_K_HELPEXE NAMES vhelp help HINTS "${BATCH_K_HELPROOT}/bin")

set(BATCH_CFG_HELPLIB	"${CMAKE_INSTALL_FULL_DATADIR}/help/batch.hlb")
set(BATCH_CFG_HELPEXE	"${BATCH_K_HELPEXE}")

configure_file("${PROJECT_SOURCE_DIR}/src/batchvers.h.in"
		"${PROJECT_BINARY_DIR}/src/batchvers.h"
		@ONLY)

#
#  Common compilation properties, expressed as an interface target so as to avoid repeating the
#  options in every target and to avoid the global CMAKE_C_FLAGS.
#
add_library(batch_options INTERFACE)

#
#  Headers reside beside the sources in the SRC directory; the second path is needed for the header
#  BATCHVERS.H, generated at configuration time into the directory of the same name in the build tree.
#
target_include_directories(batch_options INTERFACE
	"${PROJECT_SOURCE_DIR}/src"
	"${PROJECT_BINARY_DIR}/src")

target_compile_definitions(batch_options INTERFACE
	_GNU_SOURCE=1)

#
#  The gnu11 standard: GNU extensions are used, among them the dollar sign in identifiers.
#
target_compile_features(batch_options INTERFACE c_std_11)

set_target_properties(batch_options PROPERTIES
	INTERFACE_C_EXTENSIONS	ON)

target_compile_options(batch_options INTERFACE
	-Wall
	-Wextra
	-Wno-unused-parameter
	-Wformat=2
	-Wshadow
	-Wpointer-arith
	-Wwrite-strings
	$<$<BOOL:${BATCH_M_STRICT}>:-Werror>)

#
#  Vendored third-party subproject UTILITY$ROUTINES. It declares its own include path with
#  include_directories(), which is directory scoped and therefore not propagated to consumers,
#  so the path is added to batch_options below.
#
#
#  The libraries this subsystem rests upon are packages installed in the system, not copies carried
#  in the tree.  One copy of UTILITY$ROUTINES in a system is worth insisting on: the layout of its
#  structures is declared in its headers and this subsystem embeds them - the qualifier tables of
#  BATCH.C are CLI_PQDESC and the message table of BATCHMSG.C is EMSG_RECORD - so a second revision
#  in one image would not announce itself.
#
#  Both are required rather than looked for.  Neither is optional in fact: without STARLET nothing
#  here compiles, and without HELP the HELP command does not, HELPDEF.H being included plainly
#  rather than under a condition.  Saying so at configuration time is better than a diagnostic from
#  the compiler halfway through the build.
#
find_package(StarLet 1.0 REQUIRED)
find_package(HELP REQUIRED)

#  The librarian, wanted to translate the source of the help library
find_program(BATCH_K_HELPTRAN NAMES vhelptran helptran HINTS "${BATCH_K_HELPROOT}/bin")

set(BATCH_K_STARLET "${StarLet_DIR}")

if ( BATCH_K_HELPTRAN )
	set(BATCH_K_HELP "${HELP_DIR}")
else()
	set(BATCH_K_HELP "${HELP_DIR}, but HELPTRAN is not on the path")
endif()

#
#  UTILITY$ROUTINES is bound into the images. A shared library would be preferable in the ordinary
#  way - a correction to it would reach these images when the library was replaced, without waiting
#  for them to be built again - and the package offers one; but the layouts of its structures are
#  declared in its headers and this subsystem embeds them, the qualifier tables being CLI_PQDESC and
#  the message table EMSG_RECORD.
#
#  A revision that changes one of those layouts must therefore raise its SOVERSION, or the images
#  will read the new library by the old layout, and nothing will say so. That discipline is not yet
#  established: the SOVERSION has stood at 1 while the layouts moved. Until it is, binding the
#  library in is the honest choice - a mismatch is then caught by the linker, as it was before.
#
#  BATCH_M_STATICLIB=OFF asks for the shared one, for a system where the discipline is kept.
#
option(BATCH_M_STATICLIB	"Bind UTILITY$ROUTINES into the images rather than share it"	ON)

if ( (NOT BATCH_M_STATICLIB) AND (TARGET StarLet::starlet_shared) )
	set(BATCH_K_STARLETLIB StarLet::starlet_shared)
	set(BATCH_K_STARLETKIND "shared")
else()
	set(BATCH_K_STARLETLIB StarLet::starlet)
	set(BATCH_K_STARLETKIND "static")
endif()

target_link_libraries(batch_options INTERFACE ${BATCH_K_STARLETLIB} HELP::help)

if ( HELP_FOUND )
	target_link_libraries(batch_options INTERFACE HELP::help)
endif()

include(GNUInstallDirs)

#
#  Paths the generated files need. As with BATCHVERS.H, the substitution of the form @NAME@ does not
#  recognize a dollar sign within a variable name, so the values are carried through names without
#  the separator.
#
set(BATCH_CFG_SBINDIR	"${CMAKE_INSTALL_FULL_SBINDIR}")
set(BATCH_CFG_DOCDIR	"${CMAKE_INSTALL_FULL_DOCDIR}")

if ( BATCH_M_SYSTEMD )
	configure_file("${PROJECT_SOURCE_DIR}/etc/batchd.service.in"
			"${PROJECT_BINARY_DIR}/etc/batchd.service"
			@ONLY)
endif()

configure_file("${PROJECT_SOURCE_DIR}/cmake/uninstall.cmake.in"
		"${PROJECT_BINARY_DIR}/cmake/uninstall.cmake"
		@ONLY)

#
#  Removal of what the installation put in place. The manifest written during installation is the
#  record of it, so the target is available only after an installation has been made.
#
add_custom_target(uninstall
	COMMAND		"${CMAKE_COMMAND}" -P "${PROJECT_BINARY_DIR}/cmake/uninstall.cmake"
	COMMENT		"Removing what was installed from this build tree")

#
#  The manuals are rendered as PDF on request. The markdown is the source and is installed whether
#  this is asked for or not; the PDF is a convenience for reading and for printing, and it costs a
#  TeX installation, which is why it is not the default.
#
if ( BATCH_M_PDF )
	find_program(BATCH_K_PANDOC  pandoc  REQUIRED)
	find_program(BATCH_K_XELATEX xelatex REQUIRED)

	set(BATCH_K_MANUALS	batch_users_guide     batch_programmers_guide
				batch_users_guide_rus batch_programmers_guide_rus
				batch_users_guide_fra batch_programmers_guide_fra)
	set(BATCH_K_PDFS)

	#
	#  The rendered manuals are put beside the markdown they are made from, in the source tree
	#  rather than in the build tree, and are kept under version control with it. The subsystem is
	#  handed over as one set of sources, and the reader of the manual is not to be required to
	#  hold a TeX installation in order to read it.
	#
	foreach ( BATCH_K_MAN ${BATCH_K_MANUALS} )
		add_custom_command(
			OUTPUT	"${PROJECT_SOURCE_DIR}/doc/${BATCH_K_MAN}.pdf"
			COMMAND	"${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/doc/mkpdf.py"
					"${PROJECT_SOURCE_DIR}/doc/${BATCH_K_MAN}.md:${PROJECT_SOURCE_DIR}/doc/${BATCH_K_MAN}.pdf"
			DEPENDS	"${PROJECT_SOURCE_DIR}/doc/${BATCH_K_MAN}.md"
				"${PROJECT_SOURCE_DIR}/doc/mkpdf.py"
			COMMENT	"Rendering doc/${BATCH_K_MAN}.pdf")

		list(APPEND BATCH_K_PDFS "${PROJECT_SOURCE_DIR}/doc/${BATCH_K_MAN}.pdf")
	endforeach()

	add_custom_target(doc_pdf ALL DEPENDS ${BATCH_K_PDFS})

endif()

#
#  The distribution kit, named for what it holds: the facility, its version and the revision the
#  product carries. A kit whose name does not say which revision it is cannot be told from another
#  once it has left the machine it was built on.
#
#    cmake --build build --target dist
#
#
#  The kit is named for the facility and the revision it carries, in the manner of the kits of the
#  neighbouring products - UTILS-V-01-21, HELP-X-22 - so that one lies beside another and is read
#  the same way. The version is not in the name: the revision is what distinguishes one kit of a
#  version from the next, and a name that carries both says the same thing twice.
#
set(BATCH_K_KIT "${BATCH_K_FACILITY}-${BATCH_K_IDENT}")

#
#  What goes into it is named rather than swept up: the build tree lives inside the source tree more
#  often than not, and a kit assembled by copying everything would copy itself.
#
set(BATCH_K_KITDIRS	cmake contrib doc etc src test)
set(BATCH_K_KITFILES	CMakeLists.txt README.md .gitattributes .gitignore)

set(BATCH_K_KITCOPY)

foreach ( BATCH_K_ITEM ${BATCH_K_KITDIRS} )
	list(APPEND BATCH_K_KITCOPY
		COMMAND "${CMAKE_COMMAND}" -E copy_directory
			"${PROJECT_SOURCE_DIR}/${BATCH_K_ITEM}"
			"${PROJECT_BINARY_DIR}/${BATCH_K_KIT}/${BATCH_K_ITEM}")
endforeach()

foreach ( BATCH_K_ITEM ${BATCH_K_KITFILES} )
	list(APPEND BATCH_K_KITCOPY
		COMMAND "${CMAKE_COMMAND}" -E copy
			"${PROJECT_SOURCE_DIR}/${BATCH_K_ITEM}"
			"${PROJECT_BINARY_DIR}/${BATCH_K_KIT}/")
endforeach()

add_custom_target(dist
	COMMAND	"${CMAKE_COMMAND}" -E rm -rf "${PROJECT_BINARY_DIR}/${BATCH_K_KIT}"
	${BATCH_K_KITCOPY}
	COMMAND	"${CMAKE_COMMAND}" -E tar czf "${PROJECT_BINARY_DIR}/${BATCH_K_KIT}.tar.gz"
			"${BATCH_K_KIT}"
	COMMAND	"${CMAKE_COMMAND}" -E rm -rf "${PROJECT_BINARY_DIR}/${BATCH_K_KIT}"
	WORKING_DIRECTORY "${PROJECT_BINARY_DIR}"
	COMMENT	"Assembling ${BATCH_K_KIT}.tar.gz")

add_subdirectory(src)

if ( BATCH_M_TESTS )
	enable_testing()
	add_subdirectory(test)
endif()

#
#  Configuration summary.
#
message(STATUS "")
message(STATUS "  Facility ................ ${BATCH_K_FACILITY} ${PROJECT_VERSION} (${BATCH_K_IDENT})")
message(STATUS "  Distribution kit ........ ${BATCH_K_KIT}.tar.gz")
message(STATUS "  Build identifier ........ ${BATCH_CFG_BUILDID}")
message(STATUS "  Build type .............. ${CMAKE_BUILD_TYPE}")
message(STATUS "  Compiler ................ ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}")
message(STATUS "  Root directory .......... ${BATCH_K_SPOOLROOT}")
message(STATUS "  Interpreter ............. ${BATCH_K_SHELL}")
message(STATUS "  Diagnostics as errors ... ${BATCH_M_STRICT}")
message(STATUS "  Functional tests ........ ${BATCH_M_TESTS}")
message(STATUS "  UTILITY$ROUTINES ........ ${BATCH_K_STARLET}, ${BATCH_K_STARLETKIND}")
message(STATUS "  HELP .................... ${BATCH_K_HELP}")
message(STATUS "  Install prefix .......... ${CMAKE_INSTALL_PREFIX}")
message(STATUS "  Manuals as PDF .......... ${BATCH_M_PDF}")

if ( BATCH_M_SYSTEMD )
	message(STATUS "  systemd unit ............ ${BATCH_K_SYSTEMDDIR}/batchd.service")
	message(STATUS "  Started at install ...... ${BATCH_M_AUTOSTART}")
else()
	message(STATUS "  systemd unit ............ not installed")
endif()
message(STATUS "")
