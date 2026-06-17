# Dev manifest (layer/vksumi.json with relative library_path) is used in-tree
# by scripts/run.fish. The installed manifest below uses an absolute path so
# the loader finds the .so without LD_LIBRARY_PATH.

if get_option('relocatable_layer')
    lib_install_dir = join_paths(get_option('prefix'), '$LIB')
else
    lib_install_dir = join_paths(get_option('prefix'), get_option('libdir'))
endif

configure_file(
    input         : 'vksumi.json.in',
    output        : 'vksumi.json',
    configuration : {
        'LIBRARY_PATH' : join_paths(lib_install_dir, 'libVkLayer_vksumi.so'),
    },
    install       : true,
    install_dir   : join_paths(get_option('datadir'), 'vulkan', 'implicit_layer.d'),
)
