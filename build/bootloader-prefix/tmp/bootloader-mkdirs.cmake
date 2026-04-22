# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/tan11/.espressif/v6.0/esp-idf/components/bootloader/subproject"
  "/home/tan11/esp/hello_world/build/bootloader"
  "/home/tan11/esp/hello_world/build/bootloader-prefix"
  "/home/tan11/esp/hello_world/build/bootloader-prefix/tmp"
  "/home/tan11/esp/hello_world/build/bootloader-prefix/src/bootloader-stamp"
  "/home/tan11/esp/hello_world/build/bootloader-prefix/src"
  "/home/tan11/esp/hello_world/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/tan11/esp/hello_world/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/tan11/esp/hello_world/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
