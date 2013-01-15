file(GLOB labels
    RELATIVE "${PROJECT_SOURCE_DIR}/tests-data"
    "${PROJECT_SOURCE_DIR}/tests-data/label*")

list(SORT labels)

set(encoded_labels)

foreach(label ${labels})

list(APPEND encoded_labels ${label}-encoded)

add_custom_command(OUTPUT ${label}-encoded
    COMMAND "${PROJECT_SOURCE_DIR}/tests/refencode.py"
    ARGS "${PROJECT_SOURCE_DIR}/tests-data/${label}" ${label}-encoded
    DEPENDS "${PROJECT_SOURCE_DIR}/tests-data/${label}")

add_test(${label}/encoding 
    ${PROJECT_BINARY_DIR}/ordpath-test
    --encode "${PROJECT_SOURCE_DIR}/tests-data/${label}"
    --reference-data ${label}-encoded)

add_test(${label}/decoding 
    ${PROJECT_BINARY_DIR}/ordpath-test
    --decode ${label}-encoded
    --reference-data "${PROJECT_SOURCE_DIR}/tests-data/${label}")

endforeach()

add_custom_target(tests-data ALL DEPENDS ${encoded_labels})

