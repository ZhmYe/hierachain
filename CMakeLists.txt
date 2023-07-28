#------------------------------------------------------------------------------
# Top-level CMake file for FISCO-BCOS.
# ------------------------------------------------------------------------------
# This file is part of FISCO-BCOS.
#
# FISCO-BCOS is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# FISCO-BCOS is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
#
# (c) 2016-2018 fisco-dev contributors.
#------------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.12)

#set(CMAKE_CXX_COMPILER /usr/local/gcc-7.5.0/bin/g++)

set(FISCO_BCOS_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}/cmake" CACHE PATH "The path to the cmake directory")
list(APPEND CMAKE_MODULE_PATH ${FISCO_BCOS_CMAKE_DIR})
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum OS X deployment version")

# set(LINK_DIR /GPUFS/ecnu_cqjin_zhangzhao/tanghaibo/protobuf/lib)
# set(INC_DIR /GPUFS/ecnu_cqjin_zhangzhao/tanghaibo/protobuf/include)
set(LINK_DIR /usr/local/lib)
set(INC_DIR /usr/local/include)
#set(ZLIB_LINK_DIR /usr/local/zlib/lib)
#set(ZLIB_INC_DIR /usr/local/zlib/include)

project(FISCO-BCOS VERSION "2.8.0")
# Suffix like "-rc1" e.t.c. to append to versions wherever needed.
set(VERSION_SUFFIX "")

find_package(Git QUIET)
if(NOT GIT_FOUND)
  message(FATAL_ERROR "Please install git")
endif()

set(CMAKE_SKIP_INSTALL_ALL_DEPENDENCY ON)

# basic setting
include(Options)
configure_project()
include(CompilerSettings)
include(Dependencies)

# install dependencies
include(ProjectTBB)
include(ProjectSnappy)
include(ProjectLevelDB)
include(ProjectRocksDB)
include(ProjectMHD)
include(ProjectCryptopp)
include(ProjectSecp256k1)
include(ProjectJsonCpp)
include(ProjectJsonRpcCpp)
include(ProjectBoost)
include(ProjectLibzdb)
include(ProjectTCMalloc)
include(ProjectLibFF)
include(ProjectPaillier)
include(ProjectGroupSig)
include(ProjectEVMC)
include(ProjectVRF)
include(ProjectEVMONE)
include(ProjectTASSL)
# include(ProjectProtobuf)

if(USE_HSM_SDF)
  include(ProjectSDF)
endif()

include_directories("${CMAKE_SOURCE_DIR}"
		${INC_DIR}
		)

include_directories("${CMAKE_SOURCE_DIR}"
		${ZLIB_INC_DIR}
		)

add_subdirectory(libchannelserver)
add_subdirectory(libdevcore)
add_subdirectory(libdevcrypto)
add_subdirectory(libethcore)
# add_subdirectory(libinterpreter)
add_subdirectory(libstat)
add_subdirectory(libflowlimit)
add_subdirectory(libtxpool)
add_subdirectory(libstorage)
add_subdirectory(libprecompiled)
add_subdirectory(libnetwork)
add_subdirectory(libp2p)
add_subdirectory(libexecutive)
add_subdirectory(libmptstate)
add_subdirectory(libblockverifier)
add_subdirectory(libstoragestate)
add_subdirectory(libblockchain)
add_subdirectory(libsync)
add_subdirectory(libconsensus)
add_subdirectory(libledger)
add_subdirectory(librpc)
add_subdirectory(libinitializer)
add_subdirectory(libsecurity)
add_subdirectory(libeventfilter)
add_subdirectory(libplugin)
add_subdirectory(libprotobasic)
# add_subdirectory(test/unittests)

# generate executable binary fisco-bcos
#add_subdirectory(fisco-bcos)

# p2p_main.cpp
file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

if (TESTS)
    enable_testing()
    set(CTEST_OUTPUT_ON_FAILURE TRUE)
    add_subdirectory(test)
endif()

find_package(Doxygen QUIET)
if(DOXYGEN_FOUND)
# Requirements: doxygen graphviz
  set(doxyfile_in ${CMAKE_CURRENT_SOURCE_DIR}/.Doxyfile.in)
  set(doxyfile ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)
#  configure_file(${doxyfile_in} ${doxyfile} @ONLY)
# Add doc target
add_custom_target(doc COMMAND ${DOXYGEN_EXECUTABLE} ${doxyfile}
                       WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
                       COMMENT "Generating documentation with Doxygen..." VERBATIM)
elseif()
  message(WARNING "Doxygen is needed to build the documentation. Please install doxygen and graphviz")
endif()

add_executable(hierashards ${SRC_LIST} ${HEADERS})

link_directories(${LINK_DIR})

#link_directories(${ZLIB_LINK_DIR})

target_compile_options(hierashards PRIVATE -Wno-unused-variable -Wno-sign-compare -Wno-uninitialized -Wno-unused-parameter)
# target_link_libraries(mini-p2p PUBLIC initializer gRPC::grpc++)
target_link_libraries(hierashards PUBLIC ${LINK_DIR}/libprotobuf.so initializer plugin) # 链接累积性，initializer中包含了其他静态库
