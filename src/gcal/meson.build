# Builds the vendored GNOME Calendar sources (see VENDORED.md) as a static
# library. Replaces upstream's src/meson.build, which also built the
# application shell.

gcal_deps = [
  dependency('libical', version: '>= 1.0'),
  dependency('gsettings-desktop-schemas', version: '>= 3.21.2'),
  dependency('libedataserverui4-1.0', version: '>= 3.45.1'),
  dependency('libedataserver-1.2', version: '>= 3.45.1'),
  dependency('libecal-2.0', version: '>= 3.45.1'),
  dependency('libsoup-3.0'),
  dependency('libadwaita-1', version: '>= 1.8.alpha'),
  dependency('fribidi'),
  dependency('glib-2.0', version: '>= 2.80.0'),
  dependency('gtk4', version: '>= 4.21.2'),
  dependency('gio-2.0', version: '>= 2.58.0'),
  dependency('gweather4'),
  cc.find_library('m'),
]

# Upstream generates a config.h at the project root; ours is scoped to this
# directory so it cannot collide with Post's stamp-config.h.
gcal_conf = configuration_data()
gcal_conf.set_quoted('GETTEXT_PACKAGE', 'post-mail')
gcal_conf.set_quoted('PACKAGE_VERSION', '51.rc')
gcal_conf.set_quoted('VERSION', '51.rc')

# _NL_TIME_FIRST_WEEKDAY is an enum and not a define
nl_time_first_weekday_src = '''
  #include <langinfo.h>
  int main() {
    nl_langinfo(_NL_TIME_FIRST_WEEKDAY);
  };
'''
gcal_conf.set('HAVE__NL_TIME_FIRST_WEEKDAY', cc.compiles(nl_time_first_weekday_src),
  description: 'Define if _NL_TIME_FIRST_WEEKDAY is available')

# Not all systems support nl_langinfo (ALTMON_*)
altmon_src = '''
  /* _GNU_SOURCE is required by glibc 2.27.  */
  #define _GNU_SOURCE
  #include <langinfo.h>
  int main() {
    nl_langinfo (ALTMON_1);
    nl_langinfo (ALTMON_2);
    nl_langinfo (ALTMON_3);
    nl_langinfo (ALTMON_4);
    nl_langinfo (ALTMON_5);
    nl_langinfo (ALTMON_6);
    nl_langinfo (ALTMON_7);
    nl_langinfo (ALTMON_8);
    nl_langinfo (ALTMON_9);
    nl_langinfo (ALTMON_10);
    nl_langinfo (ALTMON_11);
    nl_langinfo (ALTMON_12);
  };
'''
gcal_conf.set('HAVE_ALTMON', cc.compiles(altmon_src),
  description: 'Define if ALTMON_* constants are available')

configure_file(
         output: 'config.h',
  configuration: gcal_conf
)

built_sources = gnome.mkenums_simple('gcal-enum-types', sources: files('gcal-enums.h'))

debug_conf = configuration_data()
debug_conf.set('BUGREPORT_URL', 'https://github.com/steeb-k/post/issues')
debug_conf.set10('ENABLE_TRACING', false)

built_sources += configure_file(
          input: 'gcal-debug.h.in',
         output: 'gcal-debug.h',
  configuration: debug_conf
)

calendar_incs = [include_directories('.')]

sources = files()
subdir('contrib')
subdir('core')
subdir('gui')
subdir('search')
subdir('theme')
subdir('utils')
subdir('weather')

libgcal_generated_sources_dep = declare_dependency(
  sources: built_sources,
)

gcal_deps += libgcal_generated_sources_dep

libgcal_c_args = [
  '-DHAVE_CONFIG_H',
  '-DEDS_DISABLE_DEPRECATED',
  '-DGWEATHER_I_KNOW_THIS_IS_UNSTABLE',
  '-Wno-declaration-after-statement',
  '-Wno-undef',
  '-Wno-shadow',
]

libgcal = static_library(
               'gcal-private',
              sources: sources,
  include_directories: calendar_incs,
         dependencies: gcal_deps,
               c_args: libgcal_c_args,
              install: false,
)

libgcal_dep = declare_dependency(
            link_with: libgcal,
              sources: built_sources,
         dependencies: gcal_deps,
  include_directories: calendar_incs,
)
