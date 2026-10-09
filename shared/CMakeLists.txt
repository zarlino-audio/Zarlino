add_library(zarlino_shared STATIC
    # Currently header-only, list source files here when .cpp files are added
)

target_include_directories(zarlino_shared PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/utils
    ${CMAKE_CURRENT_SOURCE_DIR}/dsp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/theme
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/primitives
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/components
    ${CMAKE_CURRENT_SOURCE_DIR}/licensing
)

# Add licensing sources (header-only API surface) via subdirectory
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/licensing)

target_link_libraries(zarlino_shared PUBLIC
    juce::juce_gui_basics
    juce::juce_gui_extra
)
