# ---------------------------------------------------------------------------
# SamaIosAssets.cmake — bundle runtime assets into an iOS .app's Resources/
#
# iOS apps load runtime assets via NSBundle, which resolves paths relative to
# the bundle's Resources/ directory.  IosFileSystem (engine/platform/ios) maps
# engine asset keys (e.g. "DamagedHelmet.glb", "fonts/JetBrainsMono-msdf.png")
# directly to those bundle paths, so the .app must mirror the engine's
# `assets/` layout — including subdirectories.
#
# This module exposes one function:
#
#   sama_ios_bundle_assets(
#       TARGET       <ios-app-target>
#       ASSETS_ROOT  <abs-path-to-assets-dir>
#       ASSETS       <relpath1> <relpath2> ...
#   )
#
# Each ASSETS entry is a path relative to ASSETS_ROOT.  The function:
#   - errors if a file is missing (catches typos at configure time, not at
#     runtime when the simulator silently logs a missing asset);
#   - calls target_sources() so the file participates in Xcode's dependency
#     graph (re-bundles on edit);
#   - sets MACOSX_PACKAGE_LOCATION per file so the resource lands at the
#     correct subdirectory under Resources/.  CMake's behavior here is:
#       MACOSX_PACKAGE_LOCATION = "Resources"        -> Resources/<basename>
#       MACOSX_PACKAGE_LOCATION = "Resources/fonts"  -> Resources/fonts/<basename>
#     i.e. it uses ONLY the basename of the source file, so we must encode
#     the subdirectory in the property value rather than relying on the file
#     path.
#
# This is the "intermediate" form of a project-driven manifest: the asset
# list lives in CMake-readable form and one helper drives the bundling.  A
# follow-up could parse a JSON manifest (engine/game/ProjectConfig style)
# and forward to this same function — keeping the bundling primitive here
# and the manifest format orthogonal.  Per-tier filtering would similarly
# slot in at the call site (e.g. compute the asset list from a TIER variable
# before calling this function), without changing the helper itself.
# ---------------------------------------------------------------------------

function(sama_ios_bundle_assets)
    set(_options)
    set(_one_value_args TARGET ASSETS_ROOT)
    set(_multi_value_args ASSETS)
    cmake_parse_arguments(_SIBA
        "${_options}"
        "${_one_value_args}"
        "${_multi_value_args}"
        ${ARGN})

    if(NOT _SIBA_TARGET)
        message(FATAL_ERROR "sama_ios_bundle_assets: TARGET is required")
    endif()
    if(NOT _SIBA_ASSETS_ROOT)
        message(FATAL_ERROR "sama_ios_bundle_assets: ASSETS_ROOT is required")
    endif()
    if(NOT IS_DIRECTORY "${_SIBA_ASSETS_ROOT}")
        message(FATAL_ERROR
            "sama_ios_bundle_assets: ASSETS_ROOT does not exist: "
            "${_SIBA_ASSETS_ROOT}")
    endif()
    if(NOT _SIBA_ASSETS)
        message(WARNING
            "sama_ios_bundle_assets: ASSETS list is empty for target "
            "${_SIBA_TARGET}; nothing to bundle")
        return()
    endif()

    foreach(_rel_path IN LISTS _SIBA_ASSETS)
        set(_abs_path "${_SIBA_ASSETS_ROOT}/${_rel_path}")
        if(NOT EXISTS "${_abs_path}")
            message(FATAL_ERROR
                "sama_ios_bundle_assets: asset not found: ${_abs_path}")
        endif()

        # Compute the bundle subdirectory.  Resources/ is the root; if the
        # relative path contains a directory component, append it so the
        # file lands at Resources/<subdir>/<basename>.
        get_filename_component(_subdir "${_rel_path}" DIRECTORY)
        if(_subdir STREQUAL "")
            set(_pkg_location "Resources")
        else()
            set(_pkg_location "Resources/${_subdir}")
        endif()

        target_sources(${_SIBA_TARGET} PRIVATE "${_abs_path}")
        set_source_files_properties("${_abs_path}" PROPERTIES
            MACOSX_PACKAGE_LOCATION "${_pkg_location}"
            HEADER_FILE_ONLY        TRUE)
    endforeach()
endfunction()
