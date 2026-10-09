# Zarlino Licensing — Shared CMake
#
# To embed a production public key, set one of the following before configuring:
#
#   cmake -B build -DZARLINO_LICENSE_PUBLIC_KEY_FILE=path/to/zarlino_license_public.txt
#
#   Or via environment variable (used by CI):
#   $env:ZARLINO_LICENSE_PUBLIC_KEY = "<modulus_hex>,<exponent_hex>"
#
# The key material is embedded into a generated header (ZarlinoPublicKey.h).
# The file / env value is a single line: "<modulus_hex>,<exponent_hex>" (as
# produced by tools/generate_license_keys.py).
#
# If neither is set, the build falls back to a placeholder dummy key
# (trial-only mode — NOT FOR PRODUCTION).

add_library(zarlino_licensing INTERFACE)

target_include_directories(zarlino_licensing INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(zarlino_licensing INTERFACE
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_cryptography
)

# --- Public key provisioning ---
# Priority: ZARLINO_LICENSE_PUBLIC_KEY_FILE > ZARLINO_LICENSE_PUBLIC_KEY env > placeholder

set(ZARLINO_PUBLIC_KEY "")
set(_zarlino_have_pubkey FALSE)

if(DEFINED ZARLINO_LICENSE_PUBLIC_KEY_FILE AND ZARLINO_LICENSE_PUBLIC_KEY_FILE)
    if(IS_ABSOLUTE "${ZARLINO_LICENSE_PUBLIC_KEY_FILE}")
        set(_zarlino_pubkey_file "${ZARLINO_LICENSE_PUBLIC_KEY_FILE}")
    else()
        # Resolve relative paths against the project root
        set(_zarlino_pubkey_file "${CMAKE_SOURCE_DIR}/${ZARLINO_LICENSE_PUBLIC_KEY_FILE}")
    endif()
    if(EXISTS "${_zarlino_pubkey_file}")
        file(READ "${_zarlino_pubkey_file}" ZARLINO_PUBLIC_KEY)
        set(_zarlino_have_pubkey TRUE)
        message(STATUS "Zarlino licensing: using public key from ${_zarlino_pubkey_file}")
    else()
        message(WARNING "ZARLINO_LICENSE_PUBLIC_KEY_FILE set but file not found: ${_zarlino_pubkey_file}")
    endif()
elseif(DEFINED ENV{ZARLINO_LICENSE_PUBLIC_KEY} AND NOT "$ENV{ZARLINO_LICENSE_PUBLIC_KEY}" STREQUAL "")
    set(ZARLINO_PUBLIC_KEY "$ENV{ZARLINO_LICENSE_PUBLIC_KEY}")
    set(_zarlino_have_pubkey TRUE)
    message(STATUS "Zarlino licensing: using public key from ZARLINO_LICENSE_PUBLIC_KEY environment variable")
endif()

if(_zarlino_have_pubkey)
    string(STRIP "${ZARLINO_PUBLIC_KEY}" _zarlino_pubkey_stripped)
    # Split "<modulus_hex>,<exponent_hex>" on the first comma
    string(FIND "${_zarlino_pubkey_stripped}" "," _zarlino_comma_idx)
    if(_zarlino_comma_idx GREATER 0)
        string(SUBSTRING "${_zarlino_pubkey_stripped}" 0 ${_zarlino_comma_idx} ZARLINO_PUBLIC_KEY_MODULUS_HEX)
        math(EXPR _zarlino_exp_start "${_zarlino_comma_idx} + 1")
        string(SUBSTRING "${_zarlino_pubkey_stripped}" ${_zarlino_exp_start} -1 ZARLINO_PUBLIC_KEY_EXPONENT_HEX)
        string(STRIP "${ZARLINO_PUBLIC_KEY_MODULUS_HEX}" ZARLINO_PUBLIC_KEY_MODULUS_HEX)
        string(STRIP "${ZARLINO_PUBLIC_KEY_EXPONENT_HEX}" ZARLINO_PUBLIC_KEY_EXPONENT_HEX)
    else()
        message(WARNING "Zarlino licensing: public key format invalid (expected \"<modulus_hex>,<exponent_hex>\")")
        set(_zarlino_have_pubkey FALSE)
    endif()
endif()

if(_zarlino_have_pubkey)
    target_compile_definitions(zarlino_licensing INTERFACE
        ZARLINO_HAVE_PUBLIC_KEY=1
    )
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/ZarlinoPublicKey.h.in
        ${CMAKE_CURRENT_BINARY_DIR}/ZarlinoPublicKey.h
        @ONLY
    )
else()
    message(STATUS "Zarlino licensing: no public key provided — using placeholder (trial-only, NOT FOR PRODUCTION)")
endif()

# Convenience: support ZARLINO_LICENSE_KEY_FILE for the runtime file-based approach
if(DEFINED ENV{ZARLINO_LICENSE_KEY_FILE} AND NOT "$ENV{ZARLINO_LICENSE_KEY_FILE}" STREQUAL "")
    target_compile_definitions(zarlino_licensing INTERFACE
        ZARLINO_LICENSE_KEY_FILE="$ENV{ZARLINO_LICENSE_KEY_FILE}"
    )
endif()
