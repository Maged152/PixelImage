# RunAndCompare.cmake - run one example, then compare its outputs to goldens.
#
# Usage:
#   cmake -DEXE=<binary> -DIN_DIR=<dir> -DOUT_DIR=<dir>
#         -DEXPECTED_DIR=<dir> -DOUTPUTS="a|b" -P RunAndCompare.cmake
#
# Normal mode:  run the example, compare SHA256 of each output with its golden.
# Update mode:  set the environment variable UPDATE_GOLDENS=1 and the fresh
#               outputs are copied into EXPECTED_DIR instead of compared.

foreach(var EXE IN_DIR OUT_DIR EXPECTED_DIR OUTPUTS)
  if(NOT DEFINED ${var})
    message(FATAL_ERROR "RunAndCompare: -D${var}=... is required")
  endif()
endforeach()

# Update mode is on when UPDATE_GOLDENS is set to anything other than 0
set(update FALSE)
if(DEFINED ENV{UPDATE_GOLDENS} AND NOT "$ENV{UPDATE_GOLDENS}" STREQUAL "0")
  set(update TRUE)
endif()

# 1) Remove stale outputs so a failed run can never pass on old files
file(MAKE_DIRECTORY "${OUT_DIR}")
foreach(f IN LISTS OUTPUTS)
  file(REMOVE "${OUT_DIR}/${f}")
endforeach()

# 2) Run the example
execute_process(
  COMMAND "${EXE}" "${IN_DIR}" "${OUT_DIR}"
  RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "Example failed (exit code ${rc}): ${EXE}")
endif()

# 3) Compare (or update) each output
set(failed FALSE)
foreach(f IN LISTS OUTPUTS)
  set(actual   "${OUT_DIR}/${f}")
  set(expected "${EXPECTED_DIR}/${f}")

  # The produced file must exist in both modes
  if(NOT EXISTS "${actual}")
    message(SEND_ERROR "Output not produced: ${actual}")
    set(failed TRUE)
    continue()
  endif()

  file(SHA256 "${actual}" actual_hash)
  file(SIZE   "${actual}" actual_size)

  if(actual_size EQUAL 0)
    message(SEND_ERROR "Output is empty: ${actual}")
    set(failed TRUE)
    continue()
  endif()

  # --- Update mode: overwrite the golden and move on ---
  if(update)
    file(MAKE_DIRECTORY "${EXPECTED_DIR}")
    file(COPY "${actual}" DESTINATION "${EXPECTED_DIR}")
    message(STATUS "UPDATED ${f}  (${actual_size} B, SHA256=${actual_hash})")
    continue()
  endif()

  # --- Normal mode: compare against the golden ---
  if(NOT EXISTS "${expected}")
    message(SEND_ERROR "Golden missing: ${expected}\n"
                       "Run the update_goldens target to create it.")
    set(failed TRUE)
    continue()
  endif()

  file(SHA256 "${expected}" expected_hash)
  file(SIZE   "${expected}" expected_size)

  if(actual_hash STREQUAL expected_hash)
    message(STATUS "OK   ${f}  (${actual_size} B, SHA256=${actual_hash})")
  else()
    message(SEND_ERROR
      "MISMATCH ${f}\n"
      "  actual:   ${actual} (${actual_size} B, SHA256=${actual_hash})\n"
      "  expected: ${expected} (${expected_size} B, SHA256=${expected_hash})\n"
      "If the change is intended, run the update_goldens target and commit.")
    set(failed TRUE)
  endif()
endforeach()

if(failed)
  message(FATAL_ERROR "Golden check failed for ${EXE}")
endif()