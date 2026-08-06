desktop_file = i18n.merge_file(
        input: 'io.github.steeb_k.Post.desktop.in',
       output: 'io.github.steeb_k.Post.desktop',
         type: 'desktop',
       po_dir: '../po',
      install: true,
  install_dir: get_option('datadir') / 'applications'
)

desktop_utils = find_program('desktop-file-validate', required: false)
if desktop_utils.found()
  test('Validate desktop file', desktop_utils, args: [desktop_file])
endif

appstream_file = i18n.merge_file(
        input: 'io.github.steeb_k.Post.metainfo.xml.in',
       output: 'io.github.steeb_k.Post.metainfo.xml',
       po_dir: '../po',
      install: true,
  install_dir: get_option('datadir') / 'metainfo'
)

appstreamcli = find_program('appstreamcli', required: false, disabler: true)
test('Validate appstream file', appstreamcli,
     args: ['validate', '--no-net', '--explain', appstream_file])

install_data('io.github.steeb_k.Post.gschema.xml',
  install_dir: get_option('datadir') / 'glib-2.0' / 'schemas'
)

# Schema for the vendored GNOME Calendar code (src/gcal)
install_data('io.github.steeb_k.Post.gcal.gschema.xml',
  install_dir: get_option('datadir') / 'glib-2.0' / 'schemas'
)

gcal_enums_xml = gnome.mkenums(
  'io.github.steeb_k.Post.gcal.enums.xml',
         sources: files('../src/gcal/gcal-enums.h'),
        comments: '<!-- @comment@ -->',
           fhead: '<schemalist>',
           vhead: '  <@type@ id="io.github.steeb_k.Post.gcal.@EnumName@">',
           vprod: '    <value nick="@valuenick@" value="@valuenum@"/>',
           vtail: '  </@type@>',
           ftail: '</schemalist>',
  install_header: true,
     install_dir: get_option('datadir') / 'glib-2.0' / 'schemas'
)

# Stage the schemas next to the generated gcal enums file so strict
# validation sees the enum definitions too
foreach schema : ['io.github.steeb_k.Post.gschema.xml', 'io.github.steeb_k.Post.gcal.gschema.xml']
  configure_file(input: schema, output: schema, copy: true)
endforeach

compile_schemas = find_program('glib-compile-schemas', required: false, disabler: true)
test('Validate schema file',
     compile_schemas,
     args: ['--strict', '--dry-run', meson.current_build_dir()],
     depends: gcal_enums_xml)

subdir('icons')
