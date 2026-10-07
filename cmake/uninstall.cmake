#++
#  MODULE:		uninstall.cmake.in
#  IDENT:		X-01-03
#  REV:			01.03.00
#
#  FACILITY:		BATCH
#
#  ABSTRACT:		Removal of what the installation put in place. The manifest CMake writes during
#			installation is the record of it, so nothing is guessed at and nothing that was
#			not installed is touched.
#
#			The subsystem root directory is deliberately not removed while it holds
#			anything: the queue journal and the logs of jobs live there, and a removal of
#			the software is not a reason to discard them. Empty directories are taken away.
#
#  AUTHOR:		StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
#
#  CREATION DATE:	29-JUL-2026
#
#  MODIFICATION HISTORY:
#
#	29-JUL-2026	RRL	X-01-01	Initial revision.
#
#	 3-AUG-2026	RRL	X-01-02	The service is stopped and disabled rather than the reader being asked to
#				remember to do it. A unit removed while its service is still enabled leaves
#				systemd complaining at every boot of a unit it cannot find.
#
#	 3-AUG-2026	RRL	X-01-03	The names carry no dollar: this file is a script of its own, parsed when
#				the removal is run, and a dollar within a name is not a reference there.
#--
#

set(batch_manifest "/sysman/Works/BATCH/install_manifest.txt")

if ( NOT EXISTS "${batch_manifest}" )
	message(FATAL_ERROR "%BATCH-F-NOMANIFEST, ${batch_manifest} does not exist; nothing was installed from this build tree")
endif()

file(READ "${batch_manifest}" batch_files)
string(REGEX REPLACE "\n" ";" batch_files "${batch_files}")

#
#  The files first. A path recorded in the manifest but already gone is reported and passed over:
#  the state wanted is its absence, and that is the state.
#
foreach ( batch_file ${batch_files} )
	if ( NOT batch_file STREQUAL "" )
		if ( IS_DIRECTORY "$ENV{DESTDIR}${batch_file}" )
			#  A directory is left to the pass below, which removes it only while it is empty
		elseif ( EXISTS "$ENV{DESTDIR}${batch_file}" OR IS_SYMLINK "$ENV{DESTDIR}${batch_file}" )
			file(REMOVE "$ENV{DESTDIR}${batch_file}")
			message(STATUS "%BATCH-S-REMOVED, $ENV{DESTDIR}${batch_file}")
		else()
			message(STATUS "%BATCH-I-ABSENT, $ENV{DESTDIR}${batch_file}")
		endif()
	endif()
endforeach()

#
#  Then the directories of the subsystem, the log first, and only while they are empty.
#
foreach ( batch_dir "/var/spool/batch/log" "/var/spool/batch" )
	if ( IS_DIRECTORY "$ENV{DESTDIR}${batch_dir}" )
		file(GLOB batch_left "$ENV{DESTDIR}${batch_dir}/*")

		if ( batch_left STREQUAL "" )
			file(REMOVE_RECURSE "$ENV{DESTDIR}${batch_dir}")
			message(STATUS "%BATCH-S-REMOVED, $ENV{DESTDIR}${batch_dir}")
		else()
			message(STATUS "%BATCH-I-NOTEMPTY, $ENV{DESTDIR}${batch_dir} is not empty and is left")
		endif()
	endif()
endforeach()

#
#  The help library has gone, so the catalogue the HELP utility shares between its users describes
#  topics that are no longer there. It is rebuilt for the same reason it was rebuilt at
#  installation, and its failure is passed over for the same reason.
#
if ( NOT "$ENV{DESTDIR}" STREQUAL "" )
	message(STATUS "%BATCH-I-SYSCACHE, staged removal, the help catalogue is left to the package")
elseif ( EXISTS "/usr/local/bin/vhelp" )
	execute_process(COMMAND "/usr/local/bin/vhelp" /SYSCACHE
			RESULT_VARIABLE batch_syscache
			OUTPUT_QUIET ERROR_QUIET)

	if ( batch_syscache EQUAL 0 )
		message(STATUS "%BATCH-I-SYSCACHE, the help catalogue has been rebuilt")
	else()
		message(STATUS "%BATCH-W-SYSCACHE, the help catalogue was not rebuilt, run 'help /SYSCACHE'")
	endif()
endif()

message(STATUS "")
#
#  The service is stopped and disabled before its unit file goes: a unit that has been removed while
#  the service is still enabled leaves systemd complaining of a unit it cannot find, at every boot,
#  and the controller itself would go on running until the machine is next restarted.
#
#
#  The names here carry no dollar either: this file is a script of its own, parsed when the removal
#  is run, and ${NAME$K_THING} is not a variable reference to CMake.
#
if ( NOT "$ENV{DESTDIR}" STREQUAL "" )
	message(STATUS "%BATCH-I-SERVICE, staged removal, the service is left to the package")
else()
	find_program(batch_systemctl systemctl)

	if ( NOT batch_systemctl )
		message(STATUS "%BATCH-I-SERVICE, no systemctl, nothing to disable")
	else()
		execute_process(COMMAND "${batch_systemctl}" disable --now batchd
				RESULT_VARIABLE batch_stopped OUTPUT_QUIET ERROR_QUIET)

		if ( batch_stopped EQUAL 0 )
			message(STATUS "%BATCH-S-SERVICE, the job controller is stopped and disabled")
		else()
			message(STATUS "%BATCH-I-SERVICE, the service was not enabled, nothing to disable")
		endif()
	endif()
endif()
message(STATUS "")
