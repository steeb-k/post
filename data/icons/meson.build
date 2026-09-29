application_id = 'io.github.steeb_k.Post'

# The app icon is drawn twice in assets/: a detailed mailbox for the large
# sizes and a plainer one that survives being small. tools/make-icons
# renders both into these fixed sizes (and the multi-size .ico) from the
# SVGs, so there is no one scalable file to install.
icon_sizes = ['512x512', '256x256', '128x128', '64x64', '48x48', '32x32']
foreach size : icon_sizes
  icon_dir = 'hicolor' / size / 'apps'
  install_data(
    icon_dir / ('@0@.png').format(application_id),
    install_dir: get_option('datadir') / 'icons' / icon_dir
  )
endforeach

symbolic_dir = 'hicolor' / 'symbolic' / 'apps'
install_data(
  symbolic_dir / ('@0@-symbolic.svg').format(application_id),
  install_dir: get_option('datadir') / 'icons' / symbolic_dir
)
