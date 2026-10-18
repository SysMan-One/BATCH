#++
#  MODULE:		test/CMakeLists.txt
#  IDENT:		X-02-02
#  REV:			02.02.00
#
#  FACILITY:		BATCH
#
#  ABSTRACT:		Registration of the functional tests. A test runs on a temporary root directory
#			and does not touch the working configuration of the subsystem, so no privileged
#			account is required.
#
#  AUTHOR:		StarLet Squad and Ruslan R. Laishev (AKA: BadAss SysMan)
#
#  CREATION DATE:	28-JUL-2026
#
#  MODIFICATION HISTORY:
#
#	28-JUL-2026	RRL	X-01-01	Initial revision.
#
#	28-JUL-2026	RRL	X-01-02	Comments converted to English.
#
#	29-JUL-2026	RRL	X-01-03	The check on the queue primitives of the vendored subproject was added.
#
#	18-OCT-2026	RRL	X-01-04	The generated System V start script is checked by the shell that will run
#				it, and for a placeholder that configure_file left unfilled.
#
#	18-OCT-2026	RRL	X-02-02	Revision raised to X-02-02 together with the product; no change of its own.
#--
#

#
#  The queue primitives of the vendored subproject are checked separately. The library is replaced
#  whole when a new revision arrives, so a defect once corrected there can return unnoticed.
#
add_executable(batch_queue queue.c)

target_link_libraries(batch_queue PRIVATE batch_options)

add_test(NAME	 batch_queue
	 COMMAND "$<TARGET_FILE:batch_queue>")

set_tests_properties(batch_queue PROPERTIES
	TIMEOUT			30
	FAIL_REGULAR_EXPRESSION	"%BATCH-[EF]-")

find_program(BATCH_K_BASH bash REQUIRED)

add_test(NAME	 batch_smoke
	 COMMAND "${BATCH_K_BASH}" "${CMAKE_CURRENT_SOURCE_DIR}/smoke.sh"
			"$<TARGET_FILE:batch>"
			"$<TARGET_FILE:batchd>"
			"${PROJECT_SOURCE_DIR}")

set_tests_properties(batch_smoke PROPERTIES
	TIMEOUT			120
	FAIL_REGULAR_EXPRESSION	"%BATCH-[EF]-")

#
#  The System V start script is generated and runs at boot, where a fault in it is seen by nobody
#  until the controller turns out not to be running. It is parsed by /bin/sh - not bash, which
#  forgives what dash does not - and looked over for an @NAME@ the configuration did not fill in.
#
if ( BATCH_M_SYSVINIT )
	add_test(NAME	 batch_initscript
		 COMMAND /bin/sh -c "/bin/sh -n \"$1\" && ! grep -n '@[A-Z_]*@' \"$1\" && echo '%BATCH-S-INITSCRIPT, parsed'"
				batch_initscript "${PROJECT_BINARY_DIR}/etc/batchd.init")

	set_tests_properties(batch_initscript PROPERTIES
		TIMEOUT			10
		PASS_REGULAR_EXPRESSION	"%BATCH-S-INITSCRIPT")
endif()
