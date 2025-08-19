# Clean a dep's build dir if its cached generator != current generator
function(clean_on_gen_mismatch build_dir)
  set(cache "${build_dir}/CMakeCache.txt")
  if (EXISTS "${cache}")
    file(STRINGS "${cache}" GEN_LINE
         REGEX "^CMAKE_GENERATOR:INTERNAL=.*$" LIMIT_COUNT 1)
    if (GEN_LINE)
      string(REGEX REPLACE "^CMAKE_GENERATOR:INTERNAL=(.+)$" "\\1" CACHED_GEN "${GEN_LINE}")
      string(STRIP "${CACHED_GEN}" CACHED_GEN)
      if (NOT CACHED_GEN STREQUAL "${CMAKE_GENERATOR}")
        message(STATUS
          "Generator mismatch in ${build_dir}: had '${CACHED_GEN}', now '${CMAKE_GENERATOR}'. Deleting that dep build dir.")
        file(REMOVE_RECURSE "${build_dir}")
      endif()
    endif()
  endif()
endfunction()
