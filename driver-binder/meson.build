project(
    'driver-binder',
    'cpp',
    version: '1.0',
    default_options: [
        'cpp_std=c++17',
    ],
)

executable(
    'ampere-driver-binder',
    [
        'driver-binder.cpp',
    ],
    dependencies: [
        dependency('systemd'),
        dependency('sdbusplus'),
        dependency('threads'),
        dependency('gpioplus'),
    ],
    install: true,
    install_dir: get_option('bindir')
)

packagedir = join_paths(
    get_option('prefix'),
    get_option('datadir'),
    meson.project_name(),
)

configfile = 'config.json'
confpath = '"' + join_paths(
    packagedir,
    configfile,
) + '"'

install_data(sources : configfile, install_dir : packagedir)

conf_data = configuration_data()
conf_data.set('CONFIG_FILE', confpath)

configure_file(output : 'config.hpp',
               configuration : conf_data)

systemd = dependency('systemd')

configure_file(
  input: 'xyz.openbmc_project.AmpDriverBinder.service',
  output: 'xyz.openbmc_project.AmpDriverBinder.service',
  copy: true,
  install_dir: systemd.get_pkgconfig_variable('systemdsystemunitdir')
  )
