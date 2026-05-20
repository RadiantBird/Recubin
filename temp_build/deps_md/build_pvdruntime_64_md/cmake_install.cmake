# Install script for directory: C:/Users/Ryarta/Documents/Recubin/temp_build/deps_md/physx/physx/pvdruntime/compiler/cmake

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "C:/Program Files/PVDRuntime")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "release")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/pvdruntime/include" TYPE FILE FILES
    "C:/Users/Ryarta/Documents/Recubin/temp_build/deps_md/physx/physx/pvdruntime/include/OmniPvdLibraryFunctions.h"
    "C:/Users/Ryarta/Documents/Recubin/temp_build/deps_md/physx/physx/pvdruntime/include/OmniPvdCommands.h"
    "C:/Users/Ryarta/Documents/Recubin/temp_build/deps_md/physx/physx/pvdruntime/include/OmniPvdDefines.h"
    "C:/Users/Ryarta/Documents/Recubin/temp_build/deps_md/physx/physx/pvdruntime/include/OmniPvdReader.h"
    "C:/Users/Ryarta/Documents/Recubin/temp_build/deps_md/physx/physx/pvdruntime/include/OmniPvdWriter.h"
    "C:/Users/Ryarta/Documents/Recubin/temp_build/deps_md/physx/physx/pvdruntime/include/OmniPvdReadStream.h"
    "C:/Users/Ryarta/Documents/Recubin/temp_build/deps_md/physx/physx/pvdruntime/include/OmniPvdWriteStream.h"
    "C:/Users/Ryarta/Documents/Recubin/temp_build/deps_md/physx/physx/pvdruntime/include/OmniPvdFileReadStream.h"
    "C:/Users/Ryarta/Documents/Recubin/temp_build/deps_md/physx/physx/pvdruntime/include/OmniPvdFileWriteStream.h"
    "C:/Users/Ryarta/Documents/Recubin/temp_build/deps_md/physx/physx/pvdruntime/include/OmniPvdMemoryStream.h"
    "C:/Users/Ryarta/Documents/Recubin/temp_build/deps_md/physx/physx/pvdruntime/include/OmniPvdLibraryHelpers.h"
    "C:/Users/Ryarta/Documents/Recubin/temp_build/deps_md/physx/physx/pvdruntime/include/OmniPvdLoader.h"
    "C:/Users/Ryarta/Documents/Recubin/temp_build/deps_md/physx/physx/pvdruntime/include/OmniPvdLoader.h"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin/win.x86_64.vc143.md/debug" TYPE STATIC_LIBRARY OPTIONAL FILES "C:/Users/Ryarta/Documents/Recubin/temp_build/deps_md/physx_out_64/lib/bin/win.x86_64.vc143.md/debug/PVDRuntime_64.lib")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Cc][Hh][Ee][Cc][Kk][Ee][Dd])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin/win.x86_64.vc143.md/checked" TYPE STATIC_LIBRARY OPTIONAL FILES "C:/Users/Ryarta/Documents/Recubin/temp_build/deps_md/physx_out_64/lib/bin/win.x86_64.vc143.md/checked/PVDRuntime_64.lib")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Pp][Rr][Oo][Ff][Ii][Ll][Ee])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin/win.x86_64.vc143.md/profile" TYPE STATIC_LIBRARY OPTIONAL FILES "C:/Users/Ryarta/Documents/Recubin/temp_build/deps_md/physx_out_64/lib/bin/win.x86_64.vc143.md/profile/PVDRuntime_64.lib")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin/win.x86_64.vc143.md/release" TYPE STATIC_LIBRARY OPTIONAL FILES "C:/Users/Ryarta/Documents/Recubin/temp_build/deps_md/physx_out_64/lib/bin/win.x86_64.vc143.md/release/PVDRuntime_64.lib")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin/win.x86_64.vc143.md/debug" TYPE SHARED_LIBRARY FILES "C:/Users/Ryarta/Documents/Recubin/temp_build/deps_md/physx_out_64/bin/bin/win.x86_64.vc143.md/debug/PVDRuntime_64.dll")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Cc][Hh][Ee][Cc][Kk][Ee][Dd])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin/win.x86_64.vc143.md/checked" TYPE SHARED_LIBRARY FILES "C:/Users/Ryarta/Documents/Recubin/temp_build/deps_md/physx_out_64/bin/bin/win.x86_64.vc143.md/checked/PVDRuntime_64.dll")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Pp][Rr][Oo][Ff][Ii][Ll][Ee])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin/win.x86_64.vc143.md/profile" TYPE SHARED_LIBRARY FILES "C:/Users/Ryarta/Documents/Recubin/temp_build/deps_md/physx_out_64/bin/bin/win.x86_64.vc143.md/profile/PVDRuntime_64.dll")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin/win.x86_64.vc143.md/release" TYPE SHARED_LIBRARY FILES "C:/Users/Ryarta/Documents/Recubin/temp_build/deps_md/physx_out_64/bin/bin/win.x86_64.vc143.md/release/PVDRuntime_64.dll")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
    include("C:/Users/Ryarta/Documents/Recubin/temp_build/deps_md/build_pvdruntime_64_md/CMakeFiles/PVDRuntime.dir/install-cxx-module-bmi-debug.cmake" OPTIONAL)
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Cc][Hh][Ee][Cc][Kk][Ee][Dd])$")
    include("C:/Users/Ryarta/Documents/Recubin/temp_build/deps_md/build_pvdruntime_64_md/CMakeFiles/PVDRuntime.dir/install-cxx-module-bmi-checked.cmake" OPTIONAL)
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Pp][Rr][Oo][Ff][Ii][Ll][Ee])$")
    include("C:/Users/Ryarta/Documents/Recubin/temp_build/deps_md/build_pvdruntime_64_md/CMakeFiles/PVDRuntime.dir/install-cxx-module-bmi-profile.cmake" OPTIONAL)
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
    include("C:/Users/Ryarta/Documents/Recubin/temp_build/deps_md/build_pvdruntime_64_md/CMakeFiles/PVDRuntime.dir/install-cxx-module-bmi-release.cmake" OPTIONAL)
  endif()
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "C:/Users/Ryarta/Documents/Recubin/temp_build/deps_md/build_pvdruntime_64_md/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
if(CMAKE_INSTALL_COMPONENT)
  if(CMAKE_INSTALL_COMPONENT MATCHES "^[a-zA-Z0-9_.+-]+$")
    set(CMAKE_INSTALL_MANIFEST "install_manifest_${CMAKE_INSTALL_COMPONENT}.txt")
  else()
    string(MD5 CMAKE_INST_COMP_HASH "${CMAKE_INSTALL_COMPONENT}")
    set(CMAKE_INSTALL_MANIFEST "install_manifest_${CMAKE_INST_COMP_HASH}.txt")
    unset(CMAKE_INST_COMP_HASH)
  endif()
else()
  set(CMAKE_INSTALL_MANIFEST "install_manifest.txt")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "C:/Users/Ryarta/Documents/Recubin/temp_build/deps_md/build_pvdruntime_64_md/${CMAKE_INSTALL_MANIFEST}"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
