if get_option('domain') != ''
  domain = get_option('domain')

  configure_file(input: 'DOMAIN.domain.rule.in',
                 output: domain + 'domain.rule',
                 configuration: conf,
                 install_dir: get_option('datadir') / 'tracker/domain-ontologies/')
  configure_file(input: 'DOMAIN.Tracker3.Miner.Extract.service.in',
                 output: domain + '.Tracker3.Miner.Extract.service',
                 configuration: conf,
                 install_dir: get_option('datadir') / 'dbus-1')
  configure_file(input: 'DOMAIN.Tracker3.Miner.Files.service.in',
                 output: domain + '.Tracker3.Miner.Files.service',
                 configuration: conf,
                 install_dir: get_option('datadir') / 'dbus-1')
endif
