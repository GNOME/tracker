# -*- coding: utf-8 -*-

import string
import random
import datetime
import os

import ontology_prefixes

output_filenames = {}
last_uris = {}
result = {}
now = datetime.datetime.today().strftime('%Y-%m-%dT%H:%M:%SZ')

####################################################################################
def addType(name, order):
  output   = '%03d-' % order + name.replace( '#', '_') + '.ttl'

  output_filenames[name] = output
  result[name] = []
  last_uris[name] = []

def addItem(itemtype, uri, content):
  last_uris[itemtype].append( uri )
  result[itemtype].append( content )

def getLastUri(type):
  return last_uris[type][-1]

def getRandomUri(type):
  return random.choice(last_uris[type])

def saveResult (output_dir=None):
  output_dir = output_dir or 'ttl'
  for ontology, content in list(result.items()):
    print('Saving', output_filenames[ontology], '...')
    path = os.path.join(output_dir, output_filenames[ontology])
    output = open(path, 'w')
    output.write( ontology_prefixes.ontology_prefixes )
    for it in content:
      output.write( it )
    output.close()
