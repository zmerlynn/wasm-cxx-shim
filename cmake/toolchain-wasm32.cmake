# wasm32-unknown-unknown CMake toolchain file.
#
# Cross-compile target: bare wasm32 (no WASI, no host platform). Works on
# any Linux / macOS / Windows host that has a wasm-capable LLVM install.
#
# Tools resolved (each can be overridden independently for mixed setups):
#   - C/C++ compiler   ← WASM_CXX_SHIM_CLANG   (or auto-detected `clang`)
#   - linker driver    ← WASM_CXX_SHIM_WASM_LD (or auto-detected `wasm-ld`)
#   - archiver         ← WASM_CXX_SHIM_AR      (or auto-detected `llvm-ar`)
#
# Convenience override: WASM_CXX_SHIM_LLVM_BIN_DIR points at a directory
# containing all three. Used if the per-tool variables aren't set.
#
# Auto-detection ladder (used only for tools not explicitly set):
#   1. ${LLVM_ROOT}/bin/   — common LLVM convention
#   2. PATH lookup, requiring `clang --print-targets` to mention wasm32
#   3. Platform-specific fallbacks (Homebrew llvm@N + lld@N on macOS,
#      /usr/lib/llvm-N/bin on Debian-family Linux)
#   4. Emscripten's bundled LLVM (last because it is large to install just
#      for this; but if it's already installed, it is complete)
#
# Pass via `cmake -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-wasm32.cmake ...`
# or via the `wasm32` preset in CMakePresets.json.

set(CMAKE_SYSTEM_NAME      Generic)
set(CMAKE_SYSTEM_PROCESSOR wasm32)

# Cross-compiling: try_compile() must produce static libraries, not
# executables — there's no wasm runtime to exec on the build host.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Don't search host /usr for libs/headers.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# ----------------------------------------------------------------------------
# Resolution helpers
# ----------------------------------------------------------------------------

# Versions to probe in homebrew/apt-style paths. Newest first; first match wins.
# Includes one version ahead of current LLVM stable (22 at time of writing) so
# the toolchain transparently picks up new LLVM releases without code changes.
set(_wasm_cxx_shim_llvm_versions 22 21 20 19 18)

# Collect candidate bin directories in priority order.
set(_wasm_cxx_shim_bin_hints "")

# (1) explicit env or cache var pointing at a complete bin dir
if(DEFINED ENV{WASM_CXX_SHIM_LLVM_BIN_DIR})
    list(APPEND _wasm_cxx_shim_bin_hints "$ENV{WASM_CXX_SHIM_LLVM_BIN_DIR}")
endif()
if(WASM_CXX_SHIM_LLVM_BIN_DIR)
    list(APPEND _wasm_cxx_shim_bin_hints "${WASM_CXX_SHIM_LLVM_BIN_DIR}")
endif()

# (2) LLVM_ROOT convention
if(DEFINED ENV{LLVM_ROOT})
    list(APPEND _wasm_cxx_shim_bin_hints "$ENV{LLVM_ROOT}/bin")
endif()
if(LLVM_ROOT)
    list(APPEND _wasm_cxx_shim_bin_hints "${LLVM_ROOT}/bin")
endif()

# (3) Platform fallbacks
if(CMAKE_HOST_APPLE)
    foreach(_v IN LISTS _wasm_cxx_shim_llvm_versions)
        list(APPEND _wasm_cxx_shim_bin_hints
            "/opt/homebrew/opt/llvm@${_v}/bin"
            "/opt/homebrew/opt/lld@${_v}/bin"
            "/usr/local/opt/llvm@${_v}/bin"
            "/usr/local/opt/lld@${_v}/bin"
        )
    endforeach()
endif()
if(CMAKE_HOST_UNIX AND NOT CMAKE_HOST_APPLE)
    foreach(_v IN LISTS _wasm_cxx_shim_llvm_versions)
        list(APPEND _wasm_cxx_shim_bin_hints "/usr/lib/llvm-${_v}/bin")
    endforeach()
endif()

# (4) Emscripten bundle. Useful as a last resort because it's a complete
# clang+wasm-ld+llvm-ar in one place.
if(DEFINED ENV{EMSDK})
    file(GLOB _emsdk_clangs "$ENV{EMSDK}/upstream/bin")
    list(APPEND _wasm_cxx_shim_bin_hints ${_emsdk_clangs})
endif()
if(CMAKE_HOST_APPLE)
    file(GLOB _brew_emcc_llvm_dirs "/opt/homebrew/Cellar/emscripten/*/libexec/llvm/bin")
    list(APPEND _wasm_cxx_shim_bin_hints ${_brew_emcc_llvm_dirs})
endif()

# ----------------------------------------------------------------------------
# Resolve clang / clang++
# ----------------------------------------------------------------------------

# Helper: verify a clang binary supports wasm32. Returns _wasm_ok in PARENT_SCOPE.
function(_wasm_cxx_shim_check_clang_wasm clang_path out_var)
    execute_process(
        COMMAND "${clang_path}" --print-targets
        OUTPUT_VARIABLE _targets
        ERROR_QUIET
        RESULT_VARIABLE _rc
        TIMEOUT 10
    )
    if(_rc EQUAL 0 AND _targets MATCHES "wasm32")
        set(${out_var} TRUE PARENT_SCOPE)
    else()
        set(${out_var} FALSE PARENT_SCOPE)
    endif()
endfunction()

# Pick clang.
set(_wasm_cxx_shim_clang "")
set(_wasm_cxx_shim_clang_was_explicit FALSE)
if(DEFINED ENV{WASM_CXX_SHIM_CLANG})
    set(_wasm_cxx_shim_clang "$ENV{WASM_CXX_SHIM_CLANG}")
    set(_wasm_cxx_shim_clang_was_explicit TRUE)
elseif(WASM_CXX_SHIM_CLANG)
    set(_wasm_cxx_shim_clang "${WASM_CXX_SHIM_CLANG}")
    set(_wasm_cxx_shim_clang_was_explicit TRUE)
else()
    # Try hint dirs first.
    foreach(_dir IN LISTS _wasm_cxx_shim_bin_hints)
        foreach(_name clang clang-22 clang-21 clang-20 clang-19 clang-18)
            if(EXISTS "${_dir}/${_name}")
                _wasm_cxx_shim_check_clang_wasm("${_dir}/${_name}" _ok)
                if(_ok)
                    set(_wasm_cxx_shim_clang "${_dir}/${_name}")
                    break()
                endif()
            endif()
        endforeach()
        if(_wasm_cxx_shim_clang)
            break()
        endif()
    endforeach()
    # Then try PATH lookup.
    if(NOT _wasm_cxx_shim_clang)
        foreach(_name clang clang-22 clang-21 clang-20 clang-19 clang-18)
            find_program(_path_clang "${_name}")
            if(_path_clang)
                _wasm_cxx_shim_check_clang_wasm("${_path_clang}" _ok)
                if(_ok)
                    set(_wasm_cxx_shim_clang "${_path_clang}")
                    break()
                endif()
            endif()
            unset(_path_clang CACHE)
        endforeach()
    endif()
endif()

if(_wasm_cxx_shim_clang_was_explicit AND (NOT _wasm_cxx_shim_clang OR NOT EXISTS "${_wasm_cxx_shim_clang}"))
    message(FATAL_ERROR
        "wasm-cxx-shim: WASM_CXX_SHIM_CLANG=${_wasm_cxx_shim_clang} was set, but the file does not exist.\n"
        "Either point it at a real wasm32-capable clang binary, or unset it to fall back to auto-detection.")
endif()

if(NOT _wasm_cxx_shim_clang OR NOT EXISTS "${_wasm_cxx_shim_clang}")
    message(FATAL_ERROR
        "wasm-cxx-shim: could not find a clang that targets wasm32.\n"
        "Set WASM_CXX_SHIM_CLANG to a clang binary, or LLVM_ROOT to an LLVM install dir.\n"
        "Install hints:\n"
        "  Linux (Debian/Ubuntu):  apt install clang-20 lld-20 llvm-20\n"
        "  Linux (apt.llvm.org):   curl -fsSL https://apt.llvm.org/llvm.sh | sudo bash -s 21\n"
        "  macOS:                  brew install llvm@20 lld@20\n"
        "  Windows:                winget install LLVM.LLVM  (or download installer from llvm.org)\n"
        "  Cross-platform:         use Emscripten's bundled LLVM if already installed (set EMSDK).\n"
        "Then re-run cmake.")
endif()

# Validate explicit override too — the most common mistake on macOS is
# pointing WASM_CXX_SHIM_CLANG at /usr/bin/clang (Apple clang has no wasm32).
if(_wasm_cxx_shim_clang_was_explicit)
    _wasm_cxx_shim_check_clang_wasm("${_wasm_cxx_shim_clang}" _ok)
    if(NOT _ok)
        message(FATAL_ERROR
            "wasm-cxx-shim: WASM_CXX_SHIM_CLANG=${_wasm_cxx_shim_clang} does not list wasm32 in --print-targets.\n"
            "(Apple clang at /usr/bin/clang and Xcode's clang both fail this check.)\n"
            "Use a clang from upstream LLVM (apt.llvm.org / homebrew llvm@N / Emscripten's bundle).")
    endif()
endif()

# Sibling clang++.
get_filename_component(_clang_dir "${_wasm_cxx_shim_clang}" DIRECTORY)
get_filename_component(_clang_name "${_wasm_cxx_shim_clang}" NAME)
string(REPLACE "clang" "clang++" _clangxx_name "${_clang_name}")
set(_wasm_cxx_shim_clangxx "${_clang_dir}/${_clangxx_name}")
if(NOT EXISTS "${_wasm_cxx_shim_clangxx}")
    # Fall back to the unsuffixed clang++ in the same directory.
    set(_wasm_cxx_shim_clangxx "${_clang_dir}/clang++")
endif()
if(NOT EXISTS "${_wasm_cxx_shim_clangxx}")
    message(FATAL_ERROR
        "wasm-cxx-shim: found clang at ${_wasm_cxx_shim_clang} but no sibling clang++.\n"
        "This usually means a broken LLVM install. Try setting WASM_CXX_SHIM_CLANG explicitly.")
endif()

# ----------------------------------------------------------------------------
# Resolve wasm-ld
# ----------------------------------------------------------------------------

set(_wasm_cxx_shim_wasm_ld "")
if(DEFINED ENV{WASM_CXX_SHIM_WASM_LD})
    set(_wasm_cxx_shim_wasm_ld "$ENV{WASM_CXX_SHIM_WASM_LD}")
elseif(WASM_CXX_SHIM_WASM_LD)
    set(_wasm_cxx_shim_wasm_ld "${WASM_CXX_SHIM_WASM_LD}")
else()
    # Sibling of clang first (most common: same install ships both).
    if(EXISTS "${_clang_dir}/wasm-ld")
        set(_wasm_cxx_shim_wasm_ld "${_clang_dir}/wasm-ld")
    else()
        # Search the hint dirs (homebrew lld@N is in a sibling formula on macOS).
        foreach(_dir IN LISTS _wasm_cxx_shim_bin_hints)
            if(EXISTS "${_dir}/wasm-ld")
                set(_wasm_cxx_shim_wasm_ld "${_dir}/wasm-ld")
                break()
            endif()
        endforeach()
    endif()
    # PATH fallback.
    if(NOT _wasm_cxx_shim_wasm_ld)
        find_program(_path_wasm_ld wasm-ld)
        if(_path_wasm_ld)
            set(_wasm_cxx_shim_wasm_ld "${_path_wasm_ld}")
        endif()
        unset(_path_wasm_ld CACHE)
    endif()
endif()

if(NOT _wasm_cxx_shim_wasm_ld OR NOT EXISTS "${_wasm_cxx_shim_wasm_ld}")
    message(FATAL_ERROR
        "wasm-cxx-shim: could not find wasm-ld.\n"
        "Found clang at: ${_wasm_cxx_shim_clang}\n"
        "Set WASM_CXX_SHIM_WASM_LD to a wasm-ld binary.\n"
        "On Homebrew macOS, wasm-ld lives in lld@N, which is a separate formula:\n"
        "  brew install lld@${_wasm_cxx_shim_default_lld_version}\n"
        "On Debian-family Linux, it ships in the lld-N package:\n"
        "  apt install lld-20")
endif()

# ----------------------------------------------------------------------------
# Resolve llvm-ar / llvm-ranlib
# ----------------------------------------------------------------------------

set(_wasm_cxx_shim_ar "")
if(DEFINED ENV{WASM_CXX_SHIM_AR})
    set(_wasm_cxx_shim_ar "$ENV{WASM_CXX_SHIM_AR}")
elseif(WASM_CXX_SHIM_AR)
    set(_wasm_cxx_shim_ar "${WASM_CXX_SHIM_AR}")
elseif(EXISTS "${_clang_dir}/llvm-ar")
    set(_wasm_cxx_shim_ar "${_clang_dir}/llvm-ar")
else()
    foreach(_dir IN LISTS _wasm_cxx_shim_bin_hints)
        if(EXISTS "${_dir}/llvm-ar")
            set(_wasm_cxx_shim_ar "${_dir}/llvm-ar")
            break()
        endif()
    endforeach()
    if(NOT _wasm_cxx_shim_ar)
        find_program(_path_ar NAMES llvm-ar llvm-ar-22 llvm-ar-21 llvm-ar-20 llvm-ar-19 llvm-ar-18)
        if(_path_ar)
            set(_wasm_cxx_shim_ar "${_path_ar}")
        endif()
        unset(_path_ar CACHE)
    endif()
endif()

if(NOT _wasm_cxx_shim_ar OR NOT EXISTS "${_wasm_cxx_shim_ar}")
    message(FATAL_ERROR
        "wasm-cxx-shim: could not find llvm-ar.\n"
        "Set WASM_CXX_SHIM_AR to a llvm-ar binary, or install LLVM via your package manager.\n"
        "Note: host `ar` (BSD ar / GNU ar) does not understand wasm object files.")
endif()

# ranlib: prefer sibling, then hint dirs, then PATH.
set(_wasm_cxx_shim_ranlib "")
get_filename_component(_ar_dir "${_wasm_cxx_shim_ar}" DIRECTORY)
if(EXISTS "${_ar_dir}/llvm-ranlib")
    set(_wasm_cxx_shim_ranlib "${_ar_dir}/llvm-ranlib")
else()
    foreach(_dir IN LISTS _wasm_cxx_shim_bin_hints)
        if(EXISTS "${_dir}/llvm-ranlib")
            set(_wasm_cxx_shim_ranlib "${_dir}/llvm-ranlib")
            break()
        endif()
    endforeach()
    if(NOT _wasm_cxx_shim_ranlib)
        find_program(_path_ranlib NAMES llvm-ranlib llvm-ranlib-22 llvm-ranlib-21 llvm-ranlib-20 llvm-ranlib-19 llvm-ranlib-18)
        if(_path_ranlib)
            set(_wasm_cxx_shim_ranlib "${_path_ranlib}")
        endif()
        unset(_path_ranlib CACHE)
    endif()
endif()
if(NOT _wasm_cxx_shim_ranlib)
    # Not strictly fatal — llvm-ar can index its own archives. But warn.
    message(WARNING "wasm-cxx-shim: llvm-ranlib not found alongside llvm-ar; relying on llvm-ar's built-in indexing.")
    set(_wasm_cxx_shim_ranlib "${_wasm_cxx_shim_ar}")
endif()

# ----------------------------------------------------------------------------
# Wire CMake variables
# ----------------------------------------------------------------------------

set(CMAKE_C_COMPILER   "${_wasm_cxx_shim_clang}"   CACHE FILEPATH "wasm32 C compiler")
set(CMAKE_CXX_COMPILER "${_wasm_cxx_shim_clangxx}" CACHE FILEPATH "wasm32 C++ compiler")
set(CMAKE_AR           "${_wasm_cxx_shim_ar}"      CACHE FILEPATH "wasm32 archiver")
set(CMAKE_RANLIB       "${_wasm_cxx_shim_ranlib}"  CACHE FILEPATH "wasm32 ranlib")

set(CMAKE_C_COMPILER_TARGET   wasm32-unknown-unknown)
set(CMAKE_CXX_COMPILER_TARGET wasm32-unknown-unknown)

# wasm-ld is the linker driver; clang invokes it via -fuse-ld=wasm-ld
# but it must be reachable. Communicate the path to consumers via a
# cache variable; downstream test/smoke uses it directly.
set(WASM_CXX_SHIM_WASM_LD "${_wasm_cxx_shim_wasm_ld}" CACHE FILEPATH "wasm-ld binary")

# Force clang to produce static archives suitable for later linking.
# (No CMAKE_EXE_LINKER_FLAGS_INIT — this toolchain produces libraries,
# not executables. The smoke test sets its own link flags.)

# Surface the resolved set in the configure log so diagnostics are easy.
message(STATUS "wasm-cxx-shim toolchain:")
message(STATUS "  CC      = ${CMAKE_C_COMPILER}")
message(STATUS "  CXX     = ${CMAKE_CXX_COMPILER}")
message(STATUS "  AR      = ${CMAKE_AR}")
message(STATUS "  RANLIB  = ${CMAKE_RANLIB}")
message(STATUS "  wasm-ld = ${WASM_CXX_SHIM_WASM_LD}")
message(STATUS "  target  = wasm32-unknown-unknown")
