#==============================================================================
# Zarlino generated product project.
#
# This file is a FIXED template: ZarlinoFactory_Generate copies it verbatim into
# every product directory. The product-specific facts live in
# generated/ProductIdentity.cmake and generated/ProductData.cpp, both of which are
# pure data. There is deliberately no per-product CMake logic, and nothing in
# this file is derived from user input.
#==============================================================================
cmake_minimum_required(VERSION 3.22)

# The generated project's own version is not meaningful; the product version
# comes from the specification and is applied to the plugin target.
# C is required as well as C++: JUCE compiles SheenBidi (and, with other formats
# enabled, other third-party C) into the graphics module.
project(ZarlinoGeneratedProduct LANGUAGES C CXX)

#------------------------------------------------------------------------------
# ZARLINO_ROOT is the checkout of the Zarlino monorepo recorded in
# generated/provenance.json. The product deliberately does not vendor JUCE, the
# factory kernel or the certified module library: those are the vendor's, they
# are versioned with the source commit in provenance, and shipping a second copy
# would let a product drift from the catalogue it was built against.
#------------------------------------------------------------------------------
if(NOT DEFINED ZARLINO_ROOT)
    message(FATAL_ERROR
        "ZARLINO_ROOT must be set to the Zarlino monorepo root, e.g.\n"
        "  cmake -S . -B build -DZARLINO_ROOT=<monorepo>")
endif()

if(NOT EXISTS "${ZARLINO_ROOT}/factory/shell/ShellTargets.cmake")
    message(FATAL_ERROR "ZARLINO_ROOT does not look like a Zarlino monorepo: ${ZARLINO_ROOT}")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Point JUCE at the VST3 SDK that ships with the monorepo.
set(JUCE_VST3_SDK_PATH "${ZARLINO_ROOT}/sdk/VST3_SDK")

include("${ZARLINO_ROOT}/factory/shell/ShellTargets.cmake")

zarlino_add_generated_product(PROJECT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
