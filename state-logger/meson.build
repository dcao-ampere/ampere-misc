project(
    'ampere-state-logger',
    'cpp',
    version: '1.0',
    default_options: [
        'cpp_std=c++17',
    ],
)

cpp = meson.get_compiler('cpp')

executable(
    'ampere-state-logger', 'ampere-state-logger.cpp',
    dependencies: [
        dependency('systemd'),
        dependency('sdbusplus'),
        dependency('threads'),
    ],
    install: true,
    install_dir: get_option('bindir')
)

systemd = dependency('systemd')

configure_file(
  input: 'xyz.openbmc_project.state_logger.service',
  output: 'xyz.openbmc_project.state_logger.service',
  copy: true,
  install_dir: systemd.get_pkgconfig_variable('systemdsystemunitdir')
  )
