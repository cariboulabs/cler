# Records 2 s of `bpsk_link --sim` at the receiver input, replays the file through a
# fresh receiver, and requires the same final message count: --record and --replay
# must hand the receiver exactly the same samples.
# Usage: cmake -DBPSK_LINK=<path> -DWORK_DIR=<dir> -P bpsk_link_replay_test.cmake
set(rec "${WORK_DIR}/bpsk_link_replay_test.cf32")

function(final_count out_var)
    execute_process(COMMAND ${ARGN} OUTPUT_VARIABLE out ERROR_VARIABLE err RESULT_VARIABLE rc TIMEOUT 60)
    if (NOT rc EQUAL 0)
        message(FATAL_ERROR "${ARGN} exited with ${rc}\n${out}\n${err}")
    endif()
    if (NOT out MATCHES "final: ([0-9]+) msgs OK  ([0-9]+) CRC fail")
        message(FATAL_ERROR "no final counters from ${ARGN}\n${out}")
    endif()
    set(${out_var} "${CMAKE_MATCH_1}" PARENT_SCOPE)
    message(STATUS "${CMAKE_MATCH_0}")
endfunction()

final_count(live "${BPSK_LINK}" --sim --headless --seconds 2 --record "${rec}")
final_count(replayed "${BPSK_LINK}" --replay "${rec}" --headless)
file(REMOVE "${rec}")

if (live LESS 5)
    message(FATAL_ERROR "the live link decoded only ${live} messages in 2 s")
endif()
if (NOT live EQUAL replayed)
    message(FATAL_ERROR "live decoded ${live} messages, replay of its recording decoded ${replayed}")
endif()
