#++
#  MODULE:		test/CMakeLists.txt
#  IDENT:		X-01-03
#  REV:			01.03.00
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
