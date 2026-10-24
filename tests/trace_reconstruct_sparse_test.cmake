if (NOT DEFINED RECONSTRUCT_EXE OR NOT DEFINED TRACE_TEST_DIR)
    message(FATAL_ERROR "RECONSTRUCT_EXE and TRACE_TEST_DIR are required")
endif()

file(REMOVE_RECURSE "${TRACE_TEST_DIR}")
file(MAKE_DIRECTORY "${TRACE_TEST_DIR}")

file(WRITE "${TRACE_TEST_DIR}/metadata.json" [=[
{
  "schema_version": 4,
  "project": "SparseReconstructTest",
  "frames_requested": 5,
  "start_frame": 0,
  "dt": 0.1,
  "trace": {
    "format": "compact_delta",
    "frames_file": "frames.jsonl",
    "schedule": {
      "mode": "periodic",
      "every_frames": 2,
      "at_frames": [],
      "frame_indexing": "absolute_zero_based_post_process"
    },
    "scenes": {
      "0": {"name": "sparse-scene"}
    }
  }
}
]=])

file(WRITE "${TRACE_TEST_DIR}/frames.jsonl" [=[{"f":0,"s":0,"k":true,"m":{"steps":1},"o":{"dot":{"kind":"particle","x":1,"y":2}}}
{"f":2,"s":0,"m":{"steps":3},"o":{"dot":{"x":3}}}
{"f":4,"s":0,"m":{"steps":5},"o":{"dot":{"x":5}}}
]=])

execute_process(
    COMMAND "${RECONSTRUCT_EXE}"
        --file "${TRACE_TEST_DIR}/frames.jsonl"
        --frame 2
        --no-index
    RESULT_VARIABLE observed_result
    OUTPUT_VARIABLE observed_output
    ERROR_VARIABLE observed_error
)
if (NOT observed_result EQUAL 0)
    message(FATAL_ERROR "Observed sparse frame reconstruction failed: ${observed_error}")
endif()
string(FIND "${observed_output}" "\"frame\":2" observed_frame_position)
string(FIND "${observed_output}" "\"steps\":3" observed_metric_position)
if (observed_frame_position EQUAL -1 OR observed_metric_position EQUAL -1)
    message(FATAL_ERROR "Observed sparse frame reconstruction returned unexpected output: ${observed_output}")
endif()

execute_process(
    COMMAND "${RECONSTRUCT_EXE}"
        --file "${TRACE_TEST_DIR}/frames.jsonl"
        --frame 1
        --no-index
    RESULT_VARIABLE unobserved_result
    OUTPUT_VARIABLE unobserved_output
    ERROR_VARIABLE unobserved_error
)
if (unobserved_result EQUAL 0)
    message(FATAL_ERROR "Unobserved sparse frame was reconstructed as if it had been traced: ${unobserved_output}")
endif()
string(FIND "${unobserved_error}" "was not observed by the semantic trace schedule" unobserved_error_position)
if (unobserved_error_position EQUAL -1)
    message(FATAL_ERROR "Unobserved sparse frame failed for the wrong reason: ${unobserved_error}")
endif()

file(REMOVE_RECURSE "${TRACE_TEST_DIR}")
