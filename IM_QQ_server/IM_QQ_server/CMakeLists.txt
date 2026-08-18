# CMakeList.txt: IM_QQ_server 的 CMake 项目，在此处包括源代码并定义
# 项目特定的逻辑。
#

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "*.cpp")

file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS "*.h")

# 将源代码添加到此项目的可执行文件。
add_executable (IM_QQ_server ${SOURCES} ${HEADERS})

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET IM_QQ_server PROPERTY CXX_STANDARD 20)
endif()

# TODO: 如有需要，请添加测试并安装目标。

add_custom_command(
    TARGET IM_QQ_server         # 绑定到 IM_QQ_server 这个 target
    POST_BUILD                  # 构建完成之后执行
    COMMAND ${CMAKE_COMMAND} -E copy_directory    # 用 cmake 自带的"复制目录"命令
        ${CMAKE_CURRENT_SOURCE_DIR}/data          # 源：当前正在处理的 CMakeLists.txt 所在目录的 data 文件夹
        $<TARGET_FILE_DIR:IM_QQ_server>/data      # 目标：可执行文件所在目录的 data 文件夹
)
