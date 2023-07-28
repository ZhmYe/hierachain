set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSTATICLIB")

file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

add_library(blockverifier ${SRC_LIST} ${HEADERS})

target_include_directories(blockverifier PRIVATE ..)
target_compile_options(blockverifier PRIVATE -Wno-unused-variable -Wno-unused-parameter -Wno-missing-field-initializers)
target_link_libraries(blockverifier PUBLIC executivecontext devcore TBB)
target_link_libraries(blockverifier PRIVATE storage precompiled)