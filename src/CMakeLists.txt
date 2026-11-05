#++
#  MODULE:		src/CMakeLists.txt
#  IDENT:		X-02-02ECO01
#  REV:			02.02.01
#
#  FACILITY:		BATCH
#
#  ABSTRACT:		Description of the build targets: the object library of common routines BATCHCOM,
#			the command line utility BATCH and the job controller BATCHD.
#
#			The common routines form an object library rather than being compiled as part of
#			each target: this precludes a second compilation of UTIL.C and guarantees that
#			both targets are built from one and the same object module.
#
#  AUTHOR:		StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
#
#  CREATION DATE:	28-JUL-2026
#
#  MODIFICATION HISTORY:
#
#	28-JUL-2026	RRL	X-01-01	Initial revision.
#
#	28-JUL-2026	RRL	X-01-02	The message table module BATCHMSG.C was added to the object library
#				BATCHCOM.
#
#	28-JUL-2026	RRL	X-01-03	Comments converted to English.
#
#	29-JUL-2026	RRL	X-01-04	The guide, the summary and the systemd unit are installed.
#
#	29-JUL-2026	RRL	X-01-05	The Programmer's Guide is installed alongside the User's Guide.
#
#	29-JUL-2026	RRL	X-01-06	The manuals rendered as PDF are installed when BATCH_M_PDF is set.
#
#	29-JUL-2026	RRL	X-01-07	The manuals in Russian are installed alongside the English ones.
#
#	30-JUL-2026	RRL	X-01-08	The rendered manuals are installed unconditionally: they belong to the
#				sources rather than to the build.
#
#	 1-AUG-2026	RRL	X-01-09	The catalogue shared by the users of the HELP utility is rebuilt after
#				the help library has been installed.
#
#	 2-AUG-2026	RRL	X-01-10	The help library exists in Russian as well and is installed into the
#				locale subdirectory the HELP facility looks in.
#
#	 3-AUG-2026	RRL	X-01-11	The job controller is enabled and started at installation, unless
#				BATCH_M_AUTOSTART says otherwise or the installation is a staged one.
#
#	 3-AUG-2026	RRL	X-01-12	The names inside the generated install scripts carry no dollar. The naming
#				of this project puts one in every name, and a script parsed at installation
#				will not have it: ${NAME$K_THING} is no variable reference to CMake, and it
#				expands to nothing with a warning rather than being refused - so the fault
#				showed itself as a service that was not started.
#
#	 4-AUG-2026	RRL	X-01-13	The help library exists in French as well. The languages are a list now
#				rather than a pair written out twice: a fourth costs a line of it.
#
#	31-AUG-2026	RRL	X-01-14	The three help libraries say the same things: PURGE, the QUEUE verb,
#					/CONFIRM and /QUEUES had been added to the English one alone.
#
#	12-SEP-2026	RRL	X-01-15	/FACILITY=${BATCH_K_FACILITY} is passed to VHELPTRAN: the library is now
#					rooted at one key, BATCH, instead of putting its thirteen topics at level
#					1 of the shared catalogue, where they collided with the topics of every
#					other product on the search path and shadowed HELP's own HELP topic.
#
#	18-OCT-2026	RRL	X-01-16	The System V start script is installed where BATCH_M_SYSVINIT asks for it,
#					and where there is no systemctl the controller is started through it:
#					update-rc.d gives it its links and the script starts it.
#
#	18-OCT-2026	RRL	X-02-02	Revision raised to X-02-02 together with the product; no change of its own.
#
#	18-OCT-2026	RRL	X-02-02ECO01	The service was never started at installation since X-01-16: the options
#					were put into the installation script as the bare words ON and OFF, which
#					IF() takes there for names of variables. They are passed as variables now.
#--
#

include(GNUInstallDirs)

#
#  BATCHCOM - common routines: logging through $LOG, the facility message table, time parsing and
#  formatting, and transfer over the socket.
#
add_library(batchcom OBJECT
	util.c
	batchmsg.c)

target_link_libraries(batchcom PUBLIC batch_options)

set_target_properties(batchcom PROPERTIES
	POSITION_INDEPENDENT_CODE	ON)

#
#  BATCH - the command line utility, installed into the directory of general-purpose images.
#
add_executable(batch
	batch.c)

target_link_libraries(batch PRIVATE batchcom batch_options)

#
#  BATCHD - the job controller, installed into the directory of system images.
#
add_executable(batchd
	batchd.c)

target_link_libraries(batchd PRIVATE batchcom batch_options)

#
#  The help library of the subsystem, translated out of its source by the librarian of the HELP
#  facility. It is built beside the sources rather than in the build tree and is kept under version
#  control with them, so that a system without HELPTRAN still installs a help library.
#
#
#  The help library exists in both languages, as the manuals do. The translation goes into the
#  locale subdirectory the HELP facility looks in, so that a reader whose locale asks for Russian
#  is answered in Russian without naming a library.
#
if ( BATCH_K_HELPTRAN )
	#
	#  One library per language: the source doc/batch_help<suffix>.md becomes doc/batch<suffix>.hlb,
	#  and the locale subdirectory it is installed into is named beside it. English carries no
	#  suffix and no subdirectory, being what a reader gets when the locale asks for nothing.
	#
	set(BATCH_K_HLBLANGS	"|;rus|ru;fra|fr")

	foreach ( BATCH_K_LANG ${BATCH_K_HLBLANGS} )
		string(REPLACE "|" ";" BATCH_K_PAIR "${BATCH_K_LANG}")
		list(GET BATCH_K_PAIR 0 BATCH_K_SFX)
		list(LENGTH BATCH_K_PAIR BATCH_K_NPAIR)

		if ( BATCH_K_NPAIR GREATER 1 )
			list(GET BATCH_K_PAIR 1 BATCH_K_LOCALE)
		else()
			set(BATCH_K_LOCALE "")
		endif()

		if ( BATCH_K_SFX STREQUAL "" )
			set(BATCH_K_HLB "batch")
			set(BATCH_K_HLBSRC "batch_help.md")
		else()
			set(BATCH_K_HLB "batch_${BATCH_K_SFX}")
			set(BATCH_K_HLBSRC "batch_help_${BATCH_K_SFX}.md")
		endif()

		add_custom_command(
			OUTPUT	"${PROJECT_SOURCE_DIR}/doc/${BATCH_K_HLB}.hlb"
			COMMAND	"${BATCH_K_HELPTRAN}" "${PROJECT_SOURCE_DIR}/doc/${BATCH_K_HLBSRC}"
					"/OUTPUT=${PROJECT_SOURCE_DIR}/doc/${BATCH_K_HLB}.hlb"
					"/IDENT=${BATCH_K_IDENT}"
					"/FACILITY=${BATCH_K_FACILITY}"
			DEPENDS	"${PROJECT_SOURCE_DIR}/doc/${BATCH_K_HLBSRC}"
			COMMENT	"Translating doc/${BATCH_K_HLBSRC} into doc/${BATCH_K_HLB}.hlb")

		list(APPEND BATCH_K_HLBS "${PROJECT_SOURCE_DIR}/doc/${BATCH_K_HLB}.hlb")
	endforeach()

	add_custom_target(batch_hlb ALL DEPENDS ${BATCH_K_HLBS})
endif()

install(FILES	"${PROJECT_SOURCE_DIR}/doc/batch.hlb"
	DESTINATION "${CMAKE_INSTALL_DATADIR}/help")

install(FILES	"${PROJECT_SOURCE_DIR}/doc/batch_rus.hlb"
	DESTINATION "${CMAKE_INSTALL_DATADIR}/help/ru")

install(FILES	"${PROJECT_SOURCE_DIR}/doc/batch_fra.hlb"
	DESTINATION "${CMAKE_INSTALL_DATADIR}/help/fr")

#
#  The catalogue the HELP utility shares between its users is harvested from the libraries of the
#  system directory once, so a library installed after it was built is not seen until it is built
#  again. It is rebuilt here, and the failure of that is reported and passed over rather than
#  carried up: the subsystem is installed either way, and a catalogue is a convenience of the HELP
#  utility rather than a part of this product. BATCH HELP does not consult it at all - it reads the
#  library directly - so what an unbuilt catalogue costs is only that "help submit" asked of the
#  general utility does not find the topic.
#
#  A staged installation is passed over altogether: DESTDIR names a directory a package is being
#  assembled in, and a catalogue built from it would describe the staging area rather than the
#  system. Whoever installs the package rebuilds it, ordinarily from the post-install script.
#
if ( BATCH_K_HELPEXE )
	install(CODE "
if ( NOT \"\$ENV{DESTDIR}\" STREQUAL \"\" )
	message(STATUS \"%BATCH-I-SYSCACHE, staged installation, the help catalogue is left to the package\")
else()
	execute_process(COMMAND \"${BATCH_K_HELPEXE}\" /SYSCACHE
			RESULT_VARIABLE batch_syscache
			OUTPUT_QUIET ERROR_QUIET)

	if ( batch_syscache EQUAL 0 )
		message(STATUS \"%BATCH-I-SYSCACHE, the help catalogue has been rebuilt\")
	else()
		message(STATUS \"%BATCH-W-SYSCACHE, the help catalogue was not rebuilt, run 'help /SYSCACHE'\")
	endif()
endif()
")
endif()

install(TARGETS	batch
	RUNTIME	DESTINATION "${CMAKE_INSTALL_BINDIR}")

install(TARGETS	batchd
	RUNTIME	DESTINATION "${CMAKE_INSTALL_SBINDIR}")

#
#  The subsystem root directory is created during installation. Mode 0755 suffices: access to jobs is
#  regulated by the job controller through the identification of the sender rather than by the file
#  system, and the socket file itself is given mode 0666 in the routine BATCHD$LISTEN.
#
install(DIRECTORY
	DESTINATION		"${BATCH_K_SPOOLROOT}"
	DIRECTORY_PERMISSIONS	OWNER_READ OWNER_WRITE OWNER_EXECUTE
				GROUP_READ GROUP_EXECUTE
				WORLD_READ WORLD_EXECUTE)

install(DIRECTORY
	DESTINATION		"${BATCH_K_SPOOLROOT}/log"
	DIRECTORY_PERMISSIONS	OWNER_READ OWNER_WRITE OWNER_EXECUTE
				GROUP_READ GROUP_EXECUTE
				WORLD_READ WORLD_EXECUTE)

#
#  The guide and the summary, so that a system carrying the software carries its description too.
#
install(FILES	"${PROJECT_SOURCE_DIR}/doc/batch_users_guide.md"
		"${PROJECT_SOURCE_DIR}/doc/batch_programmers_guide.md"
		"${PROJECT_SOURCE_DIR}/doc/batch_users_guide_rus.md"
		"${PROJECT_SOURCE_DIR}/doc/batch_programmers_guide_rus.md"
		"${PROJECT_SOURCE_DIR}/doc/batch_users_guide_fra.md"
		"${PROJECT_SOURCE_DIR}/doc/batch_programmers_guide_fra.md"
		"${PROJECT_SOURCE_DIR}/README.md"
		"${PROJECT_SOURCE_DIR}/LICENSE"
		"${PROJECT_SOURCE_DIR}/NOTICE"
	DESTINATION "${CMAKE_INSTALL_DOCDIR}")

#
#  The rendered manuals travel with the sources, so they are installed whether or not this build
#  re-made them.
#
install(FILES	"${PROJECT_SOURCE_DIR}/doc/batch_users_guide.pdf"
		"${PROJECT_SOURCE_DIR}/doc/batch_programmers_guide.pdf"
		"${PROJECT_SOURCE_DIR}/doc/batch_users_guide_rus.pdf"
		"${PROJECT_SOURCE_DIR}/doc/batch_programmers_guide_rus.pdf"
		"${PROJECT_SOURCE_DIR}/doc/batch_users_guide_fra.pdf"
		"${PROJECT_SOURCE_DIR}/doc/batch_programmers_guide_fra.pdf"
	DESTINATION "${CMAKE_INSTALL_DOCDIR}")

#
#  The systemd unit and the System V start script, whichever of them the configuration asks for.
#
if ( BATCH_M_SYSTEMD )
	install(FILES	"${PROJECT_BINARY_DIR}/etc/batchd.service"
		DESTINATION "${BATCH_K_SYSTEMDDIR}")
endif()

if ( BATCH_M_SYSVINIT )
	install(PROGRAMS "${PROJECT_BINARY_DIR}/etc/batchd.init"
		DESTINATION "${BATCH_K_INITDIR}"
		RENAME	batchd)
endif()

#
#  The controller is enabled and started where it can be. A subsystem installed and not running
#  is a subsystem that answers %BATCH-E-NOCONNECT to the first command tried on it, and the
#  reason - that nobody started it - is not what the message says. Under systemd that is
#  "systemctl enable --now"; without it, update-rc.d gives the start script its links and the
#  script starts the controller. Neither replaces a controller that is already running: a new
#  image is taken up by a restart, which is left to whoever installs.
#
#  Three cases are passed over rather than attempted. A staged installation, DESTDIR naming a
#  directory a package is assembled in: the service belongs to the system that will receive the
#  package, not to the one building it, and whoever installs it starts the service, ordinarily
#  from the post-install script. A system with neither SYSTEMCTL nor UPDATE-RC.D. And an
#  installation made by an unprivileged user, who has no service to start.
#
#  A failure is reported and passed over. The subsystem is installed either way, and a
#  controller can be started by hand; refusing the installation over it would be out of
#  proportion.
#
#  The options reach the generated script as variables, not as the bare words ON and OFF: an
#  installation script runs with policy CMP0012 unset, so IF() takes ON for the name of a variable
#  that is not defined, and the service was passed over on every system.
#
#  The names inside the generated script carry no dollar: the naming of this project puts one in
#  every name, and a script parsed at installation will not have it - ${NAME$K_THING} is not a
#  variable reference to CMake, and under the policy of the day it is a warning and an empty
#  expansion rather than a refusal, so the fault shows itself as a service that was not started
#  and not as an error in the script.
#
if ( BATCH_M_AUTOSTART AND (BATCH_M_SYSTEMD OR BATCH_M_SYSVINIT) )
	install(CODE "
if ( NOT \"\$ENV{DESTDIR}\" STREQUAL \"\" )
	message(STATUS \"%BATCH-I-AUTOSTART, staged installation, the service is left to the package\")
else()
	find_program(batch_systemctl systemctl)
	find_program(batch_updatercd update-rc.d)

	set(batch_m_systemd ${BATCH_M_SYSTEMD})
	set(batch_m_sysvinit ${BATCH_M_SYSVINIT})

	if ( batch_systemctl AND batch_m_systemd )
		execute_process(COMMAND \"\${batch_systemctl}\" daemon-reload
				RESULT_VARIABLE batch_reload OUTPUT_QUIET ERROR_QUIET)

		execute_process(COMMAND \"\${batch_systemctl}\" enable --now batchd
				RESULT_VARIABLE batch_start OUTPUT_QUIET ERROR_QUIET)

		if ( batch_start EQUAL 0 )
			message(STATUS \"%BATCH-S-AUTOSTART, the job controller is enabled and running\")
		else()
			message(STATUS \"%BATCH-W-AUTOSTART, the controller was not started, run\"
				\" systemctl enable --now batchd\")
		endif()
	elseif ( NOT batch_systemctl AND batch_updatercd AND batch_m_sysvinit )
		execute_process(COMMAND \"\${batch_updatercd}\" batchd defaults
				RESULT_VARIABLE batch_links OUTPUT_QUIET ERROR_QUIET)

		execute_process(COMMAND \"${BATCH_K_INITDIR}/batchd\" start
				RESULT_VARIABLE batch_start OUTPUT_QUIET ERROR_QUIET)

		if ( (batch_links EQUAL 0) AND (batch_start EQUAL 0) )
			message(STATUS \"%BATCH-S-AUTOSTART, the job controller is enabled and running\")
		else()
			message(STATUS \"%BATCH-W-AUTOSTART, the controller was not started, run\"
				\" update-rc.d batchd defaults; service batchd start\")
		endif()
	else()
		message(STATUS \"%BATCH-I-AUTOSTART, no service manager to use, the controller is to be started by hand\")
	endif()
endif()
")
endif()
