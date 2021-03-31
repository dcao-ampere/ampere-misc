project('ampere-cpld-fwupdate', 'c',
    default_options: [
        'buildtype=debugoptimized',
        'warning_level=3',
        'werror=true',
    ],
    version: '1.0',
)

add_project_arguments('-Wno-psabi', language: 'c')

deps = [dependency('systemd'),
]

executable('ampere-cpld-fwupdate',
           'src/main.c',
           'src/ast-jtag.c',
           'src/lattice.c',
           implicit_include_directories: false,
           include_directories: ['include'],
           dependencies: deps,
           install: true,
           install_dir: get_option('bindir'))
