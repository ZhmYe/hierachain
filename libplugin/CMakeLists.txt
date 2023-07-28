set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSTATICLIB")

file(GLOB SRC_LIST "*.cpp")
file(GLOB HEADERS "*.h")

add_library(plugin ${SRC_LIST} ${HEADERS})
target_compile_options(plugin PRIVATE -Wno-unused-variable -Wno-unused-parameter -Wno-sign-compare -Wno-pedantic -Wno-return-type -Wno-pmf-conversions -Wno-missing-field-initializers)
target_link_libraries(plugin PUBLIC channelserver rpc ledger TBB devcore protobasic)