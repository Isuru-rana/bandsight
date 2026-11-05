# SPDX-License-Identifier: GPL-3.0-or-later
# Run at build time (cmake -P) to write the short commit hash of SOURCE_DIR to
# OUTPUT. Empty when the source is not a git checkout (a release tarball) or git
# is missing. The file is only rewritten when the hash changes, so an unchanged
# commit recompiles nothing.
find_package(Git QUIET)
set(hash "")
if(GIT_FOUND)
  # Only the source tree's own repository: a tarball unpacked inside some other
  # checkout (makepkg's src/ in a packaging repo) must not report that one.
  execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${SOURCE_DIR}" rev-parse --show-toplevel
                  OUTPUT_VARIABLE top OUTPUT_STRIP_TRAILING_WHITESPACE
                  ERROR_QUIET RESULT_VARIABLE rc)
  if(rc EQUAL 0)
    file(REAL_PATH "${top}" top_real)
    file(REAL_PATH "${SOURCE_DIR}" src_real)
    if(top_real STREQUAL src_real)
      execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${SOURCE_DIR}" rev-parse --short=7 HEAD
                      OUTPUT_VARIABLE hash OUTPUT_STRIP_TRAILING_WHITESPACE
                      ERROR_QUIET RESULT_VARIABLE rc)
      if(NOT rc EQUAL 0)
        set(hash "")
      endif()
    endif()
  endif()
endif()
set(content "// Generated at build time by cmake/GitCommit.cmake. Do not edit.\n#pragma once\n#define BANDSIGHT_GIT_COMMIT \"${hash}\"\n")
set(old "")
if(EXISTS "${OUTPUT}")
  file(READ "${OUTPUT}" old)
endif()
if(NOT old STREQUAL content)
  file(WRITE "${OUTPUT}" "${content}")
endif()
