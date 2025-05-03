ufile = configure_file(output: 'upnpconfig.h', configuration: auto)

install_headers(
  'netif.h',
  'upnpdebug.h',
  'upnpdescription.h',
  'UpnpGlobal.h',
  'upnp.h',
  'upnptools.h',
  ufile,
  subdir: 'npupnp',
)
