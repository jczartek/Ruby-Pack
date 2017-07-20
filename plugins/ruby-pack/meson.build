if get_option('with_ruby_pack')

ruby_pack_sources = [
  'ide-ruby-indenter.c',
  'ide-ruby-indenter.h',
  'ruby-pack-plugin.c'
]

shared_module('ruby-pack-plugin', ruby_pack_sources,
  dependencies: plugin_deps,
  link_args: plugin_link_args,
  link_depends: plugin_link_deps,
  install: true,
  install_dir: plugindir,
)

install_data('ruby-pack.plugin', install_dir: plugindir)

endif
