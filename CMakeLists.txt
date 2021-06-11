cmake_minimum_required(VERSION 3.16)
project("gcheapstat")

if(CMAKE_SYSTEM_PROCESSOR STREQUAL x86_64 OR CMAKE_SYSTEM_PROCESSOR STREQUAL amd64 OR CMAKE_SYSTEM_PROCESSOR STREQUAL AMD64)
  message("CMAKE_SYSTEM_PROCESSOR:" ${CMAKE_SYSTEM_PROCESSOR})
else()
  message(FATAL_ERROR "Expected 64-bit build machine, found " ${CMAKE_SYSTEM_PROCESSOR})
endif()

if(WIN32)
  set(CMAKE_CXX_STANDARD_LIBRARIES "") # do not link against standard win32 libs i.e. kernel32, uuid, user32, etc.

  add_subdirectory(${CMAKE_SOURCE_DIR}/ext/json)

  add_executable(gcheapstat
    "src/main.cpp"
    "src/options.cpp"
    "src/windows/dac.cpp"
    "src/windows/main.rc"
    "src/statistics.cpp")

  target_include_directories(gcheapstat
    PUBLIC ${CMAKE_SOURCE_DIR}/ext/dac
    PUBLIC ${CMAKE_SOURCE_DIR}/ext/wil/include
    PUBLIC ${CMAKE_SOURCE_DIR}/ext/json/include
    PUBLIC ${CMAKE_SOURCE_DIR}/src/windows
    PUBLIC ${CMAKE_SOURCE_DIR}/src)

  target_precompile_headers(gcheapstat PRIVATE src/pch.h)
  target_link_libraries(gcheapstat Pathcch.lib nlohmann_json::nlohmann_json)

  if(CMAKE_BUILD_TYPE MATCHES "Release")
    # Static link MSVC runtime
    set_property(TARGET gcheapstat PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")

    # Generate pdb files for Release build
    target_compile_options(gcheapstat PRIVATE /Zi)

    # Tell linker to include symbol data
    set_target_properties(gcheapstat PROPERTIES 
      LINK_FLAGS "/INCREMENTAL:NO /DEBUG /OPT:REF /OPT:ICF"
    )

    # Set file name & location
    set_target_properties(gcheapstat PROPERTIES 
      COMPILE_PDB_NAME gcheapstat
      COMPILE_PDB_OUTPUT_DIR ${CMAKE_BINARY_DIR}
    )
  endif()
else()
  add_definitions(-DPLATFORM_UNIX=1)
  add_definitions(-DFEATURE_PAL)
  add_definitions(-DPAL_STDCPP_COMPAT=1)

  add_definitions(-D_AMD64_)
  add_definitions(-D_WIN64)
  add_definitions(-DAMD64)
  add_definitions(-DBIT64=1)
  add_definitions(-DLINUX64)

  add_compile_options(-Werror)
  add_compile_options(-Wno-format)
  add_compile_options(-Wno-null-conversion)
  # We use pshpack1/2/4/8.h and poppack.h headers to set and restore packing. However
  # clang 6.0 complains when the packing change lifetime is not contained within 
  # a header file.
  add_compile_options(-Wno-pragma-pack)
  # The following warning indicates that an attribute __attribute__((__ms_struct__)) was applied
  # to a struct or a class that has virtual members or a base class. In that case, clang
  # may not generate the same object layout as MSVC.
  add_compile_options(-Wno-incompatible-ms-struct)
  # The -fms-extensions enable the stuff like __if_exists, __declspec(uuid()), etc.
  add_compile_options(-fms-extensions)

  add_subdirectory(${CMAKE_SOURCE_DIR}/ext/json)

  add_executable(gcheapstat
    "src/main.cpp"
    "src/options.cpp"
    "src/linux/dac.cpp"
    "src/statistics.cpp")

  target_include_directories(gcheapstat
    PUBLIC ${CMAKE_SOURCE_DIR}/ext/dac
    PUBLIC ${CMAKE_SOURCE_DIR}/ext/pal
    PUBLIC ${CMAKE_SOURCE_DIR}/ext/json/include
    PUBLIC ${CMAKE_SOURCE_DIR}/src/linux
    PUBLIC ${CMAKE_SOURCE_DIR}/src)

  target_precompile_headers(gcheapstat PRIVATE src/pch.h)

  target_link_libraries(gcheapstat dl nlohmann_json::nlohmann_json ${LINKER_OPTIONS})
endif(WIN32)
