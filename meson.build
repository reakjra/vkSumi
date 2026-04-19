project('vksumi', ['cpp'],
    version         : '0.0.2',
    default_options : ['cpp_std=c++17', 'warning_level=2', 'buildtype=release'])

vulkan_dep = dependency('vulkan')

subdir('src')
subdir('layer')

install_data('scripts/vksumi-toggle',
    install_dir : get_option('bindir'),
    install_mode : 'rwxr-xr-x')
