# Find all examples source files
file(GLOB_RECURSE EXAMPLE_SOURCES *.cpp)

foreach(EXAMPLE_SOURCE ${EXAMPLE_SOURCES})
    get_filename_component(EXAMPLE_NAME ${EXAMPLE_SOURCE} NAME_WE)
    add_executable(${EXAMPLE_NAME} ${EXAMPLE_SOURCE})
    target_link_libraries(${EXAMPLE_NAME} PRIVATE qlm::PixelImage)
    set_target_properties(${EXAMPLE_NAME} PROPERTIES
                          CXX_STANDARD 20
                          CXX_EXTENSIONS OFF)
    
    add_custom_target(Example_${EXAMPLE_NAME}
        COMMAND ${CMAKE_COMMAND} -E env
                ${CMAKE_CURRENT_BINARY_DIR}/${EXAMPLE_NAME}
        # DEPENDS ${EXAMPLE_NAME}
        COMMENT "Running ${EXAMPLE_NAME} example"
    )
endforeach()