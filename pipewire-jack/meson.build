jack_inc = include_directories('.')
if get_option('jack-devel') == true
  install_subdir('jack', install_dir: get_option('includedir'), strip_directory: false)
endif
subdir('src')
