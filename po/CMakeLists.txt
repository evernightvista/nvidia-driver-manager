find_package(Gettext REQUIRED)

file(GLOB _po_files "*.po")
set(_compiled_translations)

foreach(_po_file ${_po_files})
    get_filename_component(_lang ${_po_file} NAME_WE)
    set(_mo_dir "${CMAKE_CURRENT_BINARY_DIR}/${_lang}/LC_MESSAGES")
    set(_mo_file "${_mo_dir}/${TRANSLATION_DOMAIN}.mo")

    add_custom_command(
        OUTPUT "${_mo_file}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_mo_dir}"
        COMMAND "${GETTEXT_MSGFMT_EXECUTABLE}" -o "${_mo_file}" "${_po_file}"
        DEPENDS "${_po_file}"
        COMMENT "Building translation catalog ${_lang}"
    )

    list(APPEND _compiled_translations "${_mo_file}")
    install(FILES "${_mo_file}" DESTINATION "${CMAKE_INSTALL_LOCALEDIR}/${_lang}/LC_MESSAGES")
endforeach()

add_custom_target(translations ALL DEPENDS ${_compiled_translations})