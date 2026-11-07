#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "unofficial::openfbx::openfbx" for configuration "Debug"
set_property(TARGET unofficial::openfbx::openfbx APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(unofficial::openfbx::openfbx PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "CXX"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/debug/lib/openfbx.lib"
  )

list(APPEND _cmake_import_check_targets unofficial::openfbx::openfbx )
list(APPEND _cmake_import_check_files_for_unofficial::openfbx::openfbx "${_IMPORT_PREFIX}/debug/lib/openfbx.lib" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
