cmake_minimum_required(VERSION 3.19)

# Where the test inputs, the stored golden outputs, and fresh outputs live
set(TESTS_DATA_DIR     "${CMAKE_CURRENT_SOURCE_DIR}/data")
set(TESTS_EXPECTED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/expected")
set(TESTS_OUTPUT_ROOT  "${CMAKE_BINARY_DIR}/test_output")

# add_example_test(<example_target> <output_file>...)
# Registers one CTest test named test_<example_target> that runs the example
# and compares the SHA256 of each output file against tests/expected/<file>.
function(add_example_test EXE)
  # Join with "|" so the list survives add_test; the script splits it back
  list(JOIN ARGN ";" outputs)

  add_test(
    NAME test_${EXE}
    COMMAND "${CMAKE_COMMAND}"
      "-DEXE=$<TARGET_FILE:${EXE}>"          # built example (any generator/config)
      "-DIN_DIR=${TESTS_DATA_DIR}"           # input data passed to the example
      "-DOUT_DIR=${TESTS_OUTPUT_ROOT}/${EXE}" # per-example output folder
      "-DEXPECTED_DIR=${TESTS_EXPECTED_DIR}" # stored goldens
      "-DOUTPUTS=${outputs}"                 # files to hash and compare
      -P "${CMAKE_SOURCE_DIR}/cmake/RunAndCompare.cmake")
endfunction()

# One line per example: <target> <output files produced>
add_example_test(example_image        output.jpg)
add_example_test(example_video_writer images_slide_show.mp4)
add_example_test(example_video_reader bright_car_30fps.mp4)

# Refresh goldens: re-runs every example test in update mode, which copies
# each fresh output into tests/expected/. Review with git diff, then commit.
add_custom_target(update_goldens
  COMMAND "${CMAKE_COMMAND}" -E env UPDATE_GOLDENS=1
          "${CMAKE_CTEST_COMMAND}" -C $<CONFIG> -R "^test_example_" -V
  DEPENDS example_image example_video_writer example_video_reader
  COMMENT "Regenerating tests/expected goldens (review git diff, then commit)"
  VERBATIM
)
