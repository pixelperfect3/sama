# ---------------------------------------------------------------------------
# SamaIosAssets.cmake — bundle runtime assets into an iOS .app's Resources/
#
# iOS apps load runtime assets via NSBundle, which resolves paths relative to
# the bundle's Resources/ directory.  IosFileSystem (engine/platform/ios) maps
# engine asset keys (e.g. "DamagedHelmet.glb", "fonts/JetBrainsMono-msdf.png")
# directly to those bundle paths, so the .app must mirror the engine's
# `assets/` layout — including subdirectories.
#
# This module exposes two functions:
#
#   sama_ios_bundle_assets(
#       TARGET       <ios-app-target>
#       ASSETS_ROOT  <abs-path-to-assets-dir>
#       ASSETS       <relpath1> <relpath2> ...
#   )
#
#   sama_ios_bundle_assets_from_manifest(
#       TARGET       <ios-app-target>
#       ASSETS_ROOT  <abs-path-to-assets-dir>
#       MANIFEST     <abs-path-to-project.json>
#       [TIERS       <list-of-tiers-to-include>]   # default: low;mid;high
#   )
#
# `*_from_manifest` is the JSON-driven layer: it reads a `project.json`
# with an `assets` block (common + per-tier arrays) and forwards the
# union to the primitive helper below.  The primitive's signature is
# intentionally unchanged so call sites that still want an inline list
# (or a programmatically-computed list) keep working.
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
# Two layers, separated on purpose:
#   - sama_ios_bundle_assets() is the bundling primitive — pure file I/O,
#     no awareness of project.json or tier semantics.
#   - sama_ios_bundle_assets_from_manifest() reads project.json, computes
#     the union (common + selected tiers), and forwards.
# Keeping the primitive ignorant of the manifest format means
# programmatic call sites (codegen, third-party tooling) can still drive
# the bundler with an arbitrary list, and we can swap manifest formats
# (YAML, TOML, etc.) without touching the primitive.
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

# ---------------------------------------------------------------------------
# sama_ios_bundle_assets_from_manifest — JSON-driven layer on top of the
# primitive helper.
#
#   sama_ios_bundle_assets_from_manifest(
#       TARGET       <ios-app-target>
#       ASSETS_ROOT  <abs-path-to-engine-assets-dir>
#       MANIFEST     <abs-path-to-project.json>
#       [TIERS       <list-of-tiers-to-include>]   # default: low;mid;high
#   )
#
# The manifest is JSON of the shape:
#
#   {
#       "assets": {
#           "common": [ "fonts/JetBrainsMono-msdf.png", ... ],
#           "low":    [ "models/DamagedHelmet_low.glb" ],
#           "mid":    [ "models/DamagedHelmet.glb" ],
#           "high":   [ "models/DamagedHelmet.glb", "env/cubemap_high.ktx" ]
#       }
#   }
#
# The function:
#   - parses the manifest with CMake's built-in `string(JSON ...)` (>= 3.20);
#   - computes the union of `assets.common` + every tier list named in TIERS,
#     deduplicating so the same asset isn't added twice;
#   - forwards the resulting list to the existing `sama_ios_bundle_assets()`
#     primitive (whose signature is intentionally unchanged).
#
# Default TIERS is `low;mid;high` — all tiers bundled into a single .app.
# This is the Phase-C ship target; per-tier IPA splitting is Phase D's job
# (rationale in docs/NOTES.md).  Pass TIERS to bundle a subset (e.g. for a
# space-constrained internal build).
#
# Configure-time behavior:
#   - Fatal error if the manifest does not exist or is not valid JSON.
#   - Fatal error if `assets` is not an object, or if any tier list named
#     in TIERS is present but not an array.  A *missing* tier is fine
#     (e.g. an app that only ships a "common" + "high" pair).
#   - Each forwarded asset path goes through `sama_ios_bundle_assets()`,
#     which still validates that the file exists at configure time.
# ---------------------------------------------------------------------------

function(sama_ios_bundle_assets_from_manifest)
    set(_options)
    set(_one_value_args TARGET ASSETS_ROOT MANIFEST)
    set(_multi_value_args TIERS)
    cmake_parse_arguments(_SIBAM
        "${_options}"
        "${_one_value_args}"
        "${_multi_value_args}"
        ${ARGN})

    if(NOT _SIBAM_TARGET)
        message(FATAL_ERROR
            "sama_ios_bundle_assets_from_manifest: TARGET is required")
    endif()
    if(NOT _SIBAM_ASSETS_ROOT)
        message(FATAL_ERROR
            "sama_ios_bundle_assets_from_manifest: ASSETS_ROOT is required")
    endif()
    if(NOT _SIBAM_MANIFEST)
        message(FATAL_ERROR
            "sama_ios_bundle_assets_from_manifest: MANIFEST is required")
    endif()
    if(NOT EXISTS "${_SIBAM_MANIFEST}")
        message(FATAL_ERROR
            "sama_ios_bundle_assets_from_manifest: manifest not found: "
            "${_SIBAM_MANIFEST}")
    endif()
    if(NOT _SIBAM_TIERS)
        set(_SIBAM_TIERS low mid high)
    endif()

    # Re-run CMake configure when the manifest changes so an asset added
    # to the JSON shows up without a manual reconfigure.
    set_property(DIRECTORY APPEND PROPERTY
        CMAKE_CONFIGURE_DEPENDS "${_SIBAM_MANIFEST}")

    file(READ "${_SIBAM_MANIFEST}" _manifest_json)

    # Validate top-level structure.
    string(JSON _assets_type ERROR_VARIABLE _err
        TYPE "${_manifest_json}" assets)
    if(_err OR NOT _assets_type STREQUAL "OBJECT")
        message(FATAL_ERROR
            "sama_ios_bundle_assets_from_manifest: manifest is missing an "
            "`assets` object: ${_SIBAM_MANIFEST}")
    endif()

    set(_collected_paths "")

    # Helper: pull a tier's array (if present) into _collected_paths.
    # Inlined as a macro so we can mutate _collected_paths in the parent
    # scope without juggling PARENT_SCOPE.
    macro(_siba_collect_tier _tier_name)
        string(JSON _tier_type ERROR_VARIABLE _terr
            TYPE "${_manifest_json}" assets ${_tier_name})
        if(NOT _terr)
            if(NOT _tier_type STREQUAL "ARRAY")
                message(FATAL_ERROR
                    "sama_ios_bundle_assets_from_manifest: assets.${_tier_name} "
                    "must be an array (got ${_tier_type}) in ${_SIBAM_MANIFEST}")
            endif()
            string(JSON _tier_len LENGTH "${_manifest_json}" assets ${_tier_name})
            if(_tier_len GREATER 0)
                math(EXPR _last "${_tier_len} - 1")
                foreach(_i RANGE 0 ${_last})
                    string(JSON _entry GET "${_manifest_json}"
                        assets ${_tier_name} ${_i})
                    list(APPEND _collected_paths "${_entry}")
                endforeach()
            endif()
        endif()
    endmacro()

    _siba_collect_tier("common")
    foreach(_tier IN LISTS _SIBAM_TIERS)
        _siba_collect_tier("${_tier}")
    endforeach()

    list(REMOVE_DUPLICATES _collected_paths)

    if(NOT _collected_paths)
        message(WARNING
            "sama_ios_bundle_assets_from_manifest: manifest produced an "
            "empty asset list for target ${_SIBAM_TARGET} "
            "(tiers: ${_SIBAM_TIERS}); nothing will be bundled")
        return()
    endif()

    sama_ios_bundle_assets(
        TARGET      ${_SIBAM_TARGET}
        ASSETS_ROOT ${_SIBAM_ASSETS_ROOT}
        ASSETS      ${_collected_paths})
endfunction()
