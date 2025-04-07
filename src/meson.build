stamp_sources = [
  'main.c',
  'stamp-application.c',
  'stamp-settings.c',
  'stamp-webview.c',
  'stamp-window.c',
  'backend/stamp-session.c',
  'backend/stamp-account.c',
  'views/mail/folder-list/stamp-mail-folder-list.c',
  'views/mail/folder-list/stamp-account-item-model.c',
  'views/mail/folder-list/stamp-item-model.c',
  'views/mail/folder-list/stamp-folder-item-model.c',
  'views/mail/conversation-list/stamp-mail-conversation-list.c',
  'views/mail/conversation-list/stamp-mail-conversation-list-store.c',
  'views/mail/conversation-list/stamp-mail-conversation-item-model.c',
  'views/mail/folder-list/stamp-mail-folder-row.c',
  'views/mail/conversation-list/stamp-mail-row.c',
  'views/mail/message-list/stamp-mail-message-list.c',
  'views/mail/message-list/stamp-mail-message-list-item.c',
  'views/mail/message-list/stamp-mail-message-header.c',
  'stamp-time-helpers.c',
  'views/mail/stamp-mail-view.c'
]

blueprints = custom_target('blueprints',
  input: files(
    'stamp-window.blp',
    'views/mail/folder-list/stamp-mail-folder-row.blp',
    'views/mail/conversation-list/stamp-mail-row.blp',
    'views/mail/message-list/stamp-mail-message-header.blp',
    'views/mail/message-list/stamp-mail-message-list.blp',
    'views/mail/stamp-mail-view.blp',
  ),
  output: '.',
  command: [find_program('blueprint-compiler'), 'batch-compile', '@OUTPUT@', '@CURRENT_SOURCE_DIR@', '@INPUT@'],
)

stamp_deps = [
  dependency('gtk4'),
  dependency('libadwaita-1', version: '>= 1.4'),
  dependency('camel-1.2', version: '>= 3.28'),
  dependency('libsoup-3.0'),
  dependency('libedataserver-1.2'),
  dependency('libedataserverui4-1.0'),
  dependency('webkitgtk-6.0'),
]

stamp_sources += gnome.compile_resources('stamp-resources',
  'stamp.gresource.xml',
  c_name: 'stamp',
  dependencies: blueprints,
)

stamp_includes = include_directories(
 '.',
 'backend',
 'views/mail',
 'views/mail/folder-list',
 'views/mail/conversation-list',
 'views/mail/message-list',
)

executable('stamp', stamp_sources,
  dependencies: stamp_deps,
  include_directories: stamp_includes,
       install: true,
)

subdir('web-process-extension')