# 重定义目标源码的__FILE__宏，使用相对路径的形式，避免暴露敏感信息

# This function will overwrite the standard predefined macro "__FILE__".
# "__FILE__" expands to the name of the current input file, but cmake
# input the absolute path of source file, any code using the macro 
# would expose sensitive information, such as MORDOR_THROW_EXCEPTION(x),
# so we'd better overwirte it with filename.
function(force_redefine_file_macro_for_sources targetname)
    get_target_property(source_files "${targetname}" SOURCES)
    foreach(sourcefile ${source_files})
        # Get source file's current list of compile definitions.
        get_property(defs SOURCE "${sourcefile}"
            PROPERTY COMPILE_DEFINITIONS)
        # Get the relative path of the source file in project directory
        get_filename_component(filepath "${sourcefile}" ABSOLUTE)
        string(REPLACE ${PROJECT_SOURCE_DIR}/ "" relpath ${filepath})
        list(APPEND defs "__FILE__=\"${relpath}\"")
        # Set the updated compile definitions on the source file.
        set_property(
            SOURCE "${sourcefile}"
            PROPERTY COMPILE_DEFINITIONS ${defs}
            )
    endforeach()
endfunction()

# wrapper for add_executable
# 对Cmake 自带的add_executable 命令的封装，并添加了一些额外的功能。
# 接收四个参数：目标可执行文件的名称；源文件列表；依赖项列表（这些依赖项将在编译该目标之前被构建）；需要链接的库列表

# add_executable 命令创建一个可执行文件，名字为 targetname，并将 srcs 中指定的源文件编译成该可执行文件。
# 为目标 targetname 添加依赖关系;这意味着在编译 targetname 之前，CMake 会确保 depends 中指定的所有依赖项已经被构建。

function(sylar_add_executable targetname srcs depends libs)
    add_executable(${targetname} ${srcs}) 
    add_dependencies(${targetname} ${depends})
    force_redefine_file_macro_for_sources(${targetname})
    target_link_libraries(${targetname} ${libs})
endfunction()
