# -*- coding: utf-8 -*-

import tools

####################################################################################
mto_TransferElement = '''
<%(transfer_uri)s> a mto:TransferElement ;
  mto:source        <%(transfer_source)s> ;
  mto:destination   <%(transfer_destination)s> ;
  mto:startedTime   "%(transfer_started)s" ;
  mto:completedTime "%(transfer_completed)s" ;
  mto:state         <%(transfer_state)s> .
'''
def generateTransferElement(index):
  me = 'mto#TransferElement'
  transfer_uri         = 'http://www.sopranolive.org/contexts/tracker/generated_unique_id/%d' % (index % 1000)
  transfer_source      = tools.getRandomUri( 'nmm#Photo' )
  transfer_destination = 'http://www.uploadsite%d.com/item%d' % (index % 100, index )
  transfer_started     = tools.now
  transfer_completed   = tools.now
  transfer_state       = 'http://www.tracker-project.org/temp/mto#state-done'

  tools.addItem( me, transfer_uri, mto_TransferElement % locals() )

####################################################################################
mto_UploadTransfer = '''
<%(upload_uri)s> a mto:UploadTransfer ;
  mto:transferState  <%(upload_state)s> ;
  mto:method         <%(upload_method)s> ;
  mto:created        "%(upload_created)s" ;
%(upload_transfers)s
  mto:account        "%(upload_account)s" .
'''
def generateUploadTransfer(index):
  me = 'mto#UploadTransfer'
  upload_uri     = 'mtransfer://%d' % index
  upload_state   = 'http://www.tracker-project.org/temp/mto#state-done'
  upload_method  = 'http://www.tracker-project.org/temp/mto#transfer-method-web'
  upload_created = tools.now
  upload_account = ('picasa', 'flickr', 'facebook', 'youtube')[ index % 4 ]

  # add some random transfers
  upload_transfers = ''
  for index in range (1, 2 + (index % 10)):
    upload_transfers += 'mto:transferList <%s> ;\n' % tools.getRandomUri( 'mto#TransferElement' )

  tools.addItem( me, upload_uri, mto_UploadTransfer % locals() )
