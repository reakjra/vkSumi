glslang = find_program('glslangValidator')
x11_dep = dependency('x11')

shaders = {
    'fullscreen_vert' : '../shaders/fullscreen.vert',
    'color_frag'      : '../shaders/color.frag',
}

shader_headers = []
foreach varname, src : shaders
    shader_headers += custom_target(varname + '_h',
        input   : src,
        output  : varname + '.h',
        command : [glslang, '-V', '@INPUT@', '--vn', varname, '-o', '@OUTPUT@'])
endforeach

vksumi_src = [
    'layer.cpp',
    'vkdispatch.cpp',
    'config.cpp',
    'swapchain.cpp',
    'keybinds.cpp',
]

shared_library('VkLayer_vksumi',
    vksumi_src + shader_headers,
    dependencies          : [vulkan_dep, x11_dep],
    gnu_symbol_visibility : 'hidden',
    install               : true)
