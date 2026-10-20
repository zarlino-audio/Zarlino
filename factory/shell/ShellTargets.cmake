#==============================================================================
# Zarlino factory - product target builder.
#
# ONE function turns a generated product directory into a Windows VST3, a
# standalone QA harness and a provenance/QA artifact set. It is shared by every
# product, so the build recipe cannot drift between them.
#
# Inputs (all data, no code):
#   ${PROJECT_DIR}/CMakeLists.txt                 fixed template (copied verbatim)
#   ${PROJECT_DIR}/generated/ProductIdentity.cmake  identity + build context
#   ${PROJECT_DIR}/generated/ProductData.cpp        three string literals
#
# Nothing here reads a specification directly, and nothing here interpolates
# user text into a command line, a compiler flag or a path.
#==============================================================================

function(zarlino_add_generated_product)
    cmake_parse_arguments(ZGP "" "PROJECT_DIR" "" ${ARGN})

    if(NOT ZGP_PROJECT_DIR)
        message(FATAL_ERROR "zarlino_add_generated_product requires PROJECT_DIR")
    endif()

    set(generated_dir "${ZGP_PROJECT_DIR}/generated")

    if(NOT EXISTS "${generated_dir}/ProductIdentity.cmake")
        message(FATAL_ERROR "generated product is incomplete: ${generated_dir}/ProductIdentity.cmake")
    endif()

    include("${generated_dir}/ProductIdentity.cmake")

    #--------------------------------------------------------------------------
    # JUCE and the factory kernel
    #--------------------------------------------------------------------------
    if(NOT TARGET juce::juce_core)
        add_subdirectory("${ZARLINO_ROOT}/JUCE" "${CMAKE_BINARY_DIR}/_zarlino_juce")
    endif()

    if(NOT TARGET zarlino_factory)
        # The factory's own test runner and CLIs belong to the factory's gate,
        # not to a product build.
        set(ZFACTORY_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
        add_subdirectory("${ZARLINO_ROOT}/factory" "${CMAKE_BINARY_DIR}/_zarlino_factory")
    endif()

    #--------------------------------------------------------------------------
    # Licensing. The product uses the same signing key and the same manager as
    # the hand-written Zarlino plugins; when the key is absent the build still
    # succeeds and the product reports itself as an unlicensed trial.
    #--------------------------------------------------------------------------
    set(zarlino_licensing_available FALSE)

    if(EXISTS "${ZARLINO_ROOT}/shared/licensing/CMakeLists.txt" AND TARGET juce::juce_gui_basics)
        add_subdirectory("${ZARLINO_ROOT}/shared/licensing" "${CMAKE_BINARY_DIR}/_zarlino_licensing")
        set(zarlino_licensing_available TRUE)
    endif()

    #--------------------------------------------------------------------------
    # The product plugin.
    #
    # Every identity value comes from generated/ProductIdentity.cmake, which the
    # generator derives from the validated specification; the shell sources are
    # the same for every product.
    #--------------------------------------------------------------------------
    set(zarlino_shell_sources
        "${ZARLINO_ROOT}/factory/shell/src/ProductIdentity.cpp"
        "${ZARLINO_ROOT}/factory/shell/src/ProductRuntime.cpp"
        "${ZARLINO_ROOT}/factory/shell/src/ShellProcessor.cpp"
        "${ZARLINO_ROOT}/factory/shell/src/ShellEditor.cpp"
        "${ZARLINO_ROOT}/factory/shell/src/UiTemplates.cpp"
        "${ZARLINO_ROOT}/factory/shell/src/DesignSystem.cpp"
        "${ZARLINO_ROOT}/factory/shell/src/LicenseGate.cpp"
        "${generated_dir}/ProductData.cpp"
    )

    juce_add_plugin(${ZFACTORY_TARGET_NAME}
        COMPANY_NAME "${ZFACTORY_PRODUCT_VENDOR}"
        COMPANY_WEBSITE "https://zarlinoaudio.com"
        BUNDLE_ID "${ZFACTORY_BUNDLE_ID}"
        PLUGIN_MANUFACTURER_CODE Khri
        PLUGIN_CODE ${ZFACTORY_PLUGIN_CODE}
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        FORMATS ${ZFACTORY_FORMATS}
        PRODUCT_NAME "${ZFACTORY_PRODUCT_NAME}"
        VERSION "${ZFACTORY_PRODUCT_VERSION}"
    )

    juce_generate_juce_header(${ZFACTORY_TARGET_NAME})

    target_sources(${ZFACTORY_TARGET_NAME} PRIVATE ${zarlino_shell_sources})
    target_sources(${ZFACTORY_TARGET_NAME} PRIVATE
        "${ZARLINO_ROOT}/factory/shell/src/PluginEntry.cpp")

    target_include_directories(${ZFACTORY_TARGET_NAME} PRIVATE
        "${ZARLINO_ROOT}/factory/shell/include"
    )

    target_compile_definitions(${ZFACTORY_TARGET_NAME} PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
    )

    target_link_libraries(${ZFACTORY_TARGET_NAME}
        PRIVATE
            zarlino_factory
            juce::juce_audio_utils
            juce::juce_dsp
            juce::juce_cryptography   # the shell verifies its own embedded specification hash
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
    )

    if(zarlino_licensing_available)
        target_link_libraries(${ZFACTORY_TARGET_NAME} PRIVATE zarlino_licensing)
        target_compile_definitions(${ZFACTORY_TARGET_NAME} PRIVATE ZFACTORY_HAVE_LICENSING=1)
    endif()

    #--------------------------------------------------------------------------
    # The quality gate. It links the same shell and the same generated data as
    # the plugin, so it gates exactly the code that ships.
    #--------------------------------------------------------------------------
    juce_add_console_app(${ZFACTORY_QA_TARGET_NAME} VERSION "${ZFACTORY_PRODUCT_VERSION}")
    juce_generate_juce_header(${ZFACTORY_QA_TARGET_NAME})

    target_sources(${ZFACTORY_QA_TARGET_NAME} PRIVATE
        "${ZARLINO_ROOT}/factory/shell/qa/QaMain.cpp"
        ${zarlino_shell_sources}
    )

    target_include_directories(${ZFACTORY_QA_TARGET_NAME} PRIVATE
        "${ZARLINO_ROOT}/factory/shell/include"
    )

    target_compile_definitions(${ZFACTORY_QA_TARGET_NAME} PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1
    )

    target_link_libraries(${ZFACTORY_QA_TARGET_NAME}
        PRIVATE
            zarlino_factory
            juce::juce_audio_utils
            juce::juce_audio_processors
            juce::juce_dsp
            juce::juce_cryptography
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
    )

    if(zarlino_licensing_available)
        target_link_libraries(${ZFACTORY_QA_TARGET_NAME} PRIVATE zarlino_licensing)
        target_compile_definitions(${ZFACTORY_QA_TARGET_NAME} PRIVATE ZFACTORY_HAVE_LICENSING=1)
    endif()

    #--------------------------------------------------------------------------
    # A provenance stamp the build can read without parsing C++.
    #--------------------------------------------------------------------------
    add_custom_command(TARGET ${ZFACTORY_TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ZGP_PROJECT_DIR}/provenance.json"
            "$<TARGET_FILE_DIR:${ZFACTORY_TARGET_NAME}>/provenance.json"
        COMMENT "Stamping build provenance next to the plugin binary"
    )
endfunction()
