if not get_option('testmains')
  subdir_done()
endif

tests = [
  'test_description',
  'test_init',
  'test_netif',
  'test_url',
]

foreach t : tests
  test(t, executable(t, '@0@.cpp'.format(t), dependencies: libnpupnp_dep))
endforeach
