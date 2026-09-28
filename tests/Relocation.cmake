if(NOT DEFINED APP OR NOT DEFINED FORGE OR NOT DEFINED WORK OR NOT DEFINED ASSETS)
    message(FATAL_ERROR "Expected APP, FORGE, WORK and ASSETS")
endif()
set(stage "${WORK}/RTS проверка пути с пробелами")
file(MAKE_DIRECTORY "${stage}")
file(COPY "${ASSETS}" DESTINATION "${stage}")
# Assets in the current directory must not substitute for missing exe-local assets.
set(missing "${stage}/Без ресурсов")
file(MAKE_DIRECTORY "${missing}")
foreach(executable IN ITEMS "${APP}" "${FORGE}")
    get_filename_component(appName "${executable}" NAME)
    file(COPY "${executable}" DESTINATION "${stage}")
    execute_process(COMMAND "${stage}/${appName}" --verify-assets
        WORKING_DIRECTORY "${WORK}" RESULT_VARIABLE result TIMEOUT 20)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Relocated ${appName} could not load assets: ${result}")
    endif()
    file(COPY "${executable}" DESTINATION "${missing}")
    execute_process(COMMAND "${missing}/${appName}" --verify-assets
        WORKING_DIRECTORY "${stage}" RESULT_VARIABLE result TIMEOUT 20)
    if(NOT result EQUAL 1)
        message(FATAL_ERROR "${appName}: expected missing-assets exit code 1, got ${result}")
    endif()
endforeach()

# Includes F9 -> game --map with unsaved changes -> F10 -> preserved Forge document.
get_filename_component(forgeName "${FORGE}" NAME)
execute_process(COMMAND "${stage}/${forgeName}" --smoke-test
    WORKING_DIRECTORY "${WORK}" RESULT_VARIABLE result TIMEOUT 40)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Relocated Forge/game test play failed: ${result}")
endif()
