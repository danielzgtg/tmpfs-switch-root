add_executable(gather_file_info gather_file_info.cpp trie.cpp trie.h work_file.h)
add_executable(build_ramdisk build_ramdisk.cpp work_file.h)
add_executable(tmpfs_switch_init
        tmpfs_switch_init.c
        tmpfs_switch_init.h
        tmpfs_switch_proc.c
        tmpfs_switch_tty.c
        work_file.h
)

configure_file(../config_example/exclude_paths.txt config/exclude_paths.txt COPYONLY)
configure_file(../config_example/include_dirs.txt config/include_dirs.txt COPYONLY)
configure_file(../config_example/include_packages.txt config/include_packages.txt COPYONLY)
