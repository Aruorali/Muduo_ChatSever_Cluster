
aux_source_directory(. SRC_LIST)
aux_source_directory(../database DATABASE_SRC)
add_executable(ChatServer ${SRC_LIST} ${DATABASE_SRC} ../main.cpp)


target_link_libraries(ChatServer muduo_net muduo_base pthread mysqlclient)
