cmake_minimum_required (VERSION 3.6)

project (ampere-cpld-fwupdate C)

set (CMAKE_CXX_STANDARD 17)
set (CMAKE_CXX_STANDARD_REQUIRED ON)

# boost
find_package (Boost 1.66 REQUIRED)
include_directories (${BOOST_SRC_DIR})
add_definitions (-DBOOST_ERROR_CODE_HEADER_ONLY)
add_definitions (-DBOOST_SYSTEM_NO_DEPRECATED)
add_definitions (-DBOOST_ALL_NO_LIB)
add_definitions (-DBOOST_NO_TYPEID)
add_definitions (-DBOOST_ASIO_DISABLE_THREADS)

# ampere-cpld-fwupdate
add_executable (ampere-cpld-fwupdate src/main.c src/ast-jtag.c src/lattice.c)
include_directories (${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries (ampere-cpld-fwupdate sdbusplus systemd)
install (TARGETS ampere-cpld-fwupdate DESTINATION bin)
