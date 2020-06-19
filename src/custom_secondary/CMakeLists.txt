set(SOURCES managedsecondary.cc custom-secondary.cc)

set(HEADERS managedsecondary.h custom-secondary.h)

set(TARGET custom_secondary)

add_library(${TARGET} STATIC
  ${SOURCES}
)

target_include_directories(${TARGET} PUBLIC ${PROJECT_SOURCE_DIR}/src/custom_secondary)

target_link_libraries(custom_secondary)

aktualizr_source_file_checks(${HEADERS} ${SOURCES} ${TEST_SOURCES})
