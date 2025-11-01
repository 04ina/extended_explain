project('extended_explain', ['c'])

pg_config = get_option('pg_config')

if pg_config == 'default'
    pg_config = find_program('pg_config')
endif

bindir = run_command(pg_config, '--bindir', check: true).stdout().strip()
includedir_server = run_command(pg_config, '--includedir-server', check: true).stdout().strip()
pkglibdir = run_command(pg_config, '--pkglibdir', check: true).stdout().strip()
sharedir = run_command(pg_config, '--sharedir', check: true).stdout().strip()

shared_module('extended_explain', 'extended_explain.c', 'output_result.c',
              include_directories: [includedir_server],
              install: true,
              install_dir: pkglibdir,
              name_prefix: '',
             )

install_data('extended_explain.control',
             'extended_explain--1.0.sql',
             install_dir: sharedir / 'extension',
            )

pg_regress = find_program('pg_regress',
                          dirs: [pkglibdir / 'pgxs/src/test/regress']
                         )

regress_tests = ['eepaths', 'eequery']

test('regress',
     pg_regress,
     args: ['--bindir', bindir,
            '--inputdir', meson.current_source_dir() / 'test',
           ] + regress_tests,
    )