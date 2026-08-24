#load subdirectories
aux_source_directory(./server SRC_LIST)
aux_source_directory(./database DATABASE_SRC)
aux_source_directory(./redis REDIS_SRC)
add_executable(ChatServer ${SRC_LIST} ${DATABASE_SRC} ${REDIS_SRC} ./main.cpp)


target_link_libraries(ChatServer muduo_net muduo_base pthread mysqlclient hiredis)