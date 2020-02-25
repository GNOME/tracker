# These tools are used for generating the ontologies documentation.

ttl_loader_files = [
    'ttl_loader.c',
    'ttl_model.c',
    'ttl_xml.c',
]

ttl2xml = executable('ttl2xml',
    ttl_loader_files, 'ttl2xml.c', 'ttlresource2xml.c',
    dependencies: [tracker_data_dep, tracker_sparql_dep])
