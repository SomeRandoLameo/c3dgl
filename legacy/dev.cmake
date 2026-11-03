# Cross-platform build/run/send helper, used by the Zed tasks in .zed/tasks.json.
#
#   cmake -P scripts/dev.cmake build            configure + build into build/
#   cmake -P scripts/dev.cmake run [app.3dsx]   build, then launch in an emulator
#   cmake -P scripts/dev.cmake launch [app.3dsx] launch without building
#   cmake -P scripts/dev.cmake send [app.3dsx]   build, then send with 3dslink
#
# Emulators searched: Azahar, Lime3DS, Mandarine, Citra (PATH, app bundles,
# Flatpak, common Windows install dirs). Set C3DGL_EMULATOR to override.
#
# 3dslink searched in PATH. Set C3DGL_3DSLINK to override.

cmake_minimum_required(VERSION 3.13)

get_filename_component(ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(BUILD_DIR "${ROOT}/build")

set(ACTION run)
if(CMAKE_ARGC GREATER 3)
    set(ACTION "${CMAKE_ARGV3}")
endif()

set(ROM "${BUILD_DIR}/examples/cube/c3dgl_cube.3dsx")
if(CMAKE_ARGC GREATER 4)
    get_filename_component(ROM "${CMAKE_ARGV4}" ABSOLUTE BASE_DIR "${ROOT}")
endif()


function(c3dgl_build)
    if(CMAKE_HOST_WIN32)
        # devkitPro's toolchain only works with CMake running inside its msys2
        find_program(MSYS2_BASH bash
            HINTS
                "$ENV{C3DGL_MSYS2}/usr/bin"
                "C:/devkitPro/msys2/usr/bin"
            NO_DEFAULT_PATH)

        if(NOT MSYS2_BASH)
            message(FATAL_ERROR
                "devkitPro msys2 not found "
                "(expected C:/devkitPro/msys2; set C3DGL_MSYS2 to its root)")
        endif()

        set(ENV{MSYSTEM} MSYS)
        set(ENV{CHERE_INVOKING} 1)

        execute_process(
            COMMAND "${MSYS2_BASH}" -lc
                    "cmake -S . -B build && cmake --build build"
            WORKING_DIRECTORY "${ROOT}"
            RESULT_VARIABLE rc)

    else()
        if(NOT DEFINED ENV{DEVKITPRO})
            set(ENV{DEVKITPRO} /opt/devkitpro)
        endif()

        execute_process(
            COMMAND "${CMAKE_COMMAND}"
                    -S "${ROOT}"
                    -B "${BUILD_DIR}"
            RESULT_VARIABLE rc)

        if(rc EQUAL 0)
            execute_process(
                COMMAND "${CMAKE_COMMAND}"
                        --build "${BUILD_DIR}"
                RESULT_VARIABLE rc)
        endif()
    endif()

    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "build failed")
    endif()
endfunction()


function(c3dgl_launch)
    if(NOT EXISTS "${ROM}")
        message(FATAL_ERROR "${ROM} not found (build first)")
    endif()

    set(emu_cmd "")

    if(DEFINED ENV{C3DGL_EMULATOR})
        set(emu_cmd "$ENV{C3DGL_EMULATOR}")

    else()
        set(CMAKE_FIND_APPBUNDLE NEVER)

        set(hints "")

        foreach(dir "/Applications" "$ENV{HOME}/Applications")
            list(APPEND hints
                "${dir}/Azahar.app/Contents/MacOS"
                "${dir}/Lime3DS.app/Contents/MacOS"
                "${dir}/mandarine-qt.app/Contents/MacOS"
                "${dir}/citra-qt.app/Contents/MacOS"
                "${dir}")
        endforeach()

        foreach(dir
            "$ENV{LOCALAPPDATA}"
            "$ENV{LOCALAPPDATA}/Programs"
            "$ENV{ProgramFiles}")
            if(dir)
                list(APPEND hints
                    "${dir}/Azahar"
                    "${dir}/Lime3DS"
                    "${dir}/Mandarine"
                    "${dir}/Citra"
                    "${dir}/citra/nightly"
                    "${dir}/citra/canary")
            endif()
        endforeach()

        find_program(EMU
            NAMES
                azahar
                azahar-qt
                Azahar
                lime3ds
                lime3ds-gui
                lime3ds-qt
                mandarine-qt
                mandarine
                citra-qt
                citra
            HINTS ${hints})

        if(EMU)
            set(emu_cmd "${EMU}")

        else()
            find_program(FLATPAK flatpak)

            if(FLATPAK)
                foreach(app
                    org.azahar_emu.Azahar
                    io.github.lime3ds.Lime3DS)

                    execute_process(
                        COMMAND "${FLATPAK}" info "${app}"
                        RESULT_VARIABLE rc
                        OUTPUT_QUIET
                        ERROR_QUIET)

                    if(rc EQUAL 0)
                        set(emu_cmd
                            "${FLATPAK}"
                            run
                            "--filesystem=${ROOT}"
                            "${app}")
                        break()
                    endif()
                endforeach()
            endif()
        endif()
    endif()

    if(NOT emu_cmd)
        message(FATAL_ERROR
            "no 3DS emulator found "
            "(install Azahar or set C3DGL_EMULATOR)")
    endif()

    list(JOIN emu_cmd " " emu_str)

    message(STATUS "Running ${ROM} with ${emu_str}")

    execute_process(
        COMMAND ${emu_cmd} "${ROM}"
        RESULT_VARIABLE rc)

    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "emulator exited with code ${rc}")
    endif()
endfunction()


function(c3dgl_send)
    if(NOT EXISTS "${ROM}")
        message(FATAL_ERROR "${ROM} not found (build first)")
    endif()

    if(DEFINED ENV{C3DGL_3DSLINK})
        set(THREEDS_LINK "$ENV{C3DGL_3DSLINK}")
    else()
        find_program(THREEDS_LINK
            NAMES
                3dslink
                3dslink.exe)
    endif()

    if(NOT THREEDS_LINK)
        message(FATAL_ERROR
            "3dslink not found. "
            "Install devkitPro's 3dslink package or set "
            "C3DGL_3DSLINK to the 3dslink executable.")
    endif()

    message(STATUS "Sending ${ROM} with ${THREEDS_LINK}")

    execute_process(
        COMMAND "${THREEDS_LINK}" "${ROM}"
        WORKING_DIRECTORY "${ROOT}"
        RESULT_VARIABLE rc)

    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "3dslink failed with exit code ${rc}")
    endif()
endfunction()


if(ACTION STREQUAL "build")

    c3dgl_build()

elseif(ACTION STREQUAL "run")

    c3dgl_build()
    c3dgl_launch()

elseif(ACTION STREQUAL "launch")

    c3dgl_launch()

elseif(ACTION STREQUAL "send")

    c3dgl_build()
    c3dgl_send()

else()

    message(FATAL_ERROR
        "unknown action '${ACTION}' "
        "(build, run, launch, send)")

endif()