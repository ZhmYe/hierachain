file(GLOB SRC_LIST "*.cc")
file(GLOB HEADERS "*.h")

#find_package(Protobuf CONFIG REQUIRED)
#find_package(gRPC CONFIG REQUIRED)
#set(_PROTOBUF_LIBPROTOBUF protobuf::libprotobuf)

add_library(protobasic ${SRC_LIST} ${HEADERS})
target_compile_options(protobasic PRIVATE -Wno-unused-variable -Wno-unused-parameter -Wno-zero-length-array)
target_link_libraries(protobasic PUBLIC protobuf)