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

executable('ampere_cpld_fwupdate',
           'src/main.c',
           'src/ast-jtag.c',
           'src/ast-jtag-intf.c',
           'src/lattice.c',
           'src/cpld.c',
           implicit_include_directories: false,
           include_directories: ['include'],
           dependencies: deps,
           install: true,
           install_dir: get_option('bindir'))
