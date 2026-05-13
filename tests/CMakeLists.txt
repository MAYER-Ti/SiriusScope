add_executable(tst_core_domain
    domain/tst_core_domain.cpp
)

target_link_libraries(tst_core_domain
    PRIVATE
        siriusscope_core
)

add_test(NAME tst_core_domain COMMAND tst_core_domain)

add_executable(tst_processing
    processing/tst_processing.cpp
)

target_link_libraries(tst_processing
    PRIVATE
        siriusscope_processing
)

add_test(NAME tst_processing COMMAND tst_processing)
