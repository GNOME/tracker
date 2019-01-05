#!/usr/bin/python
#
# Copyright (C) 2010, Nokia <ivan.frade@nokia.com>
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
# 02110-1301, USA.
#

"""
Stand-alone tests cases for the store, inserting, removing information
in pure sparql and checking that the data is really there
"""
import sys,os
import unittest
import time
import random
import string
import datetime

from common.utils import configuration as cfg
import unittest as ut
#import unittest as ut
from common.utils.storetest import CommonTrackerStoreTest as CommonTrackerStoreTest

class TrackerStoreInsertionTests (CommonTrackerStoreTest):
	"""
        Insert single and multiple-valued properties, dates (ok and broken)
        and check the results
	"""

	def test_insert_01 (self):
		"""
                Simple insert of two triplets.

                1. Insert a InformationElement with title.
                2. TEST: Query the title of that information element
                3. Remove the InformationElement to keep everything as it was before
                """

                uri = "tracker://test_insert_01/" + str(random.randint (0, 100))
                insert = """
                INSERT { <%s> a nie:InformationElement;
                        nie:title \"test_insert_01\". }
                """ % (uri)
                self.tracker.update (insert)

		""" verify the inserted item """
                query = """
                SELECT ?t WHERE {
                <%s> a nie:InformationElement ;
                nie:title ?t .
                }
                """ % (uri)
                results = self.tracker.query (query)

                self.assertEquals (str(results[0][0]), "test_insert_01")

		""" delete the inserted item """
                delete = """
                DELETE { <%s> a rdfs:Resource. }
                """ % (uri)
                self.tracker.update (delete)


	def test_insert_02(self):
                """
                Insert of a bigger set of triplets (linking two objects)
                """

                self.tracker.update("""
                INSERT {
                <urn:uuid:bob-dylan> a nmm:Artist;
                   nmm:artistName 'Bob Dylan'.

                <file:///a/b/c/10_song3.mp3> a nmm:MusicPiece, nfo:FileDataObject;
                   nfo:fileName 'subterranean-homesick-blues.mp3';
                   nfo:fileLastModified '2008-10-23T13:47:02' ;
                   nfo:fileCreated '2008-12-16T12:41:20' ;
                   nfo:fileSize 17630 ;
                   nfo:duration 219252 ;
                   nie:title 'Subterranean homesick blues';
                   nmm:performer <urn:uuid:bob-dylan>.
                   }
                   """)

                QUERY = """
                SELECT ?uri ?title ?length  WHERE {
                    ?uri a nmm:MusicPiece ;
                         nmm:performer <urn:uuid:bob-dylan> ;
                         nie:title ?title ;
                         nfo:duration ?length .
                }
                """

                result = self.tracker.query (QUERY)
                self.assertEquals (len (result), 1)
                self.assertEquals (len (result[0]), 3) # uri, title, length
                self.assertEquals (result[0][0], "file:///a/b/c/10_song3.mp3")
                self.assertEquals (result[0][1], "Subterranean homesick blues")
                self.assertEquals (result[0][2], "219252")

                self.tracker.update ("""
                DELETE {
                   <urn:uuid:bob-dylan> a rdfs:Resource.
                   <file:///a/b/c/10_song3.mp3> a rdfs:Resource.
                }
                """)


        def test_insert_03(self):
                """
                Checking all the values are inserted
                """

		self.tracker.update("""
                INSERT {
                <urn:uuid:7646004> a nmm:Artist;
                    nmm:artistName 'John Lennon' .

                <urn:uuid:123123123> a nmm:MusicAlbum ;
                    nie:title 'Imagine' .

                <file:///a/b/c/imagine.mp3> a nmm:MusicPiece, nfo:FileDataObject;
                    nfo:fileName 'imagine.mp3';
                    nfo:fileCreated '2008-12-16T12:41:20';
                    nfo:fileLastModified '2008-12-23T13:47:02' ;
                    nfo:fileSize 17630;
                    nmm:musicAlbum <urn:uuid:123123123>;
                    nmm:trackNumber '11';
                    nfo:duration 219252;
                    nmm:performer <urn:uuid:7646004>.
                    }

                    """)

                QUERY = """
                SELECT ?artist ?length ?trackN ?album ?size ?flm ?fc ?filename  WHERE {
                    <file:///a/b/c/imagine.mp3> a nmm:MusicPiece ;
                        nmm:performer ?x ;
                        nfo:duration ?length ;
                        nmm:trackNumber ?trackN ;
                        nmm:musicAlbum ?y ;
                        nfo:fileSize ?size ;
                        nfo:fileLastModified ?flm ;
                        nfo:fileCreated ?fc ;
                        nfo:fileName ?filename.

                    ?x nmm:artistName ?artist .
                    ?y nie:title ?album.
                    }
                    """
                result = self.tracker.query(QUERY)

                self.assertEquals (len (result), 1)
                self.assertEquals (len (result[0]), 8)
                self.assertEquals (result[0][0], "John Lennon")
                self.assertEquals (result[0][1], "219252")
                self.assertEquals (result[0][2], "11")
                self.assertEquals (result[0][3], "Imagine")
                self.assertEquals (result[0][4], "17630")
                # FIXME Tracker returns this translated to the current timezone
                #self.assertEquals (result[0][5], "2008-12-23T11:47:02Z")
                #self.assertEquals (result[0][6], "2008-12-16T10:41:20Z")
                self.assertEquals (result[0][7], "imagine.mp3")

                self.tracker.update ("""
                DELETE {
                   <urn:uuid:123123123> a rdfs:Resource .
                }

                DELETE {
                  <file:///a/b/c/imagine.mp3> a rdfs:Resource.
                }
                """)




	def test_insert_04(self):
                """
                Insert, delete same single valued properties multiple times.
                """
                for i in range (0, 3):
                        # Delete single valued properties of music file.
                        self.tracker.update("""
                        DELETE {
                          <test://instance-1> nie:usageCounter ?v
                        } WHERE {
                          <test://instance-1> nie:usageCounter ?v .
                        }
                        DELETE {
                          <test://instance-1> nie:contentAccessed ?w .
                        } WHERE {
                          <test://instance-1> nie:contentAccessed ?w .
                        }
                        """)

                        # Insert the same single valued properties of music file.
                        self.tracker.update("""
                        INSERT {
                           <test://instance-1> a nmm:MusicPiece, nfo:FileDataObject;
                           nie:usageCounter '%d';
                           nie:contentAccessed '2000-01-01T00:4%d:47Z' .
                        }""" % (i, i))

                        # Query for the property values and verify whether the last change is applied.
                        result = self.tracker.query ("""
                          SELECT ?playcount ?date WHERE {
                             <test://instance-1> a nmm:MusicPiece ;
                                 nie:usageCounter ?playcount ;
                                 nie:contentAccessed ?date.
                          }""")

                        self.assertEquals (len (result), 1)
                        self.assertEquals (len (result[0]), 2)
                        self.assertEquals (int (result[0][0]), i)
                        self.assertEquals (result[0][1], "2000-01-01T00:4%d:47Z" % (i))

                self.tracker.update ("""
                DELETE { <test://instance-1> a rdfs:Resource. }
                """)


	def test_insert_05(self):
                """
                Insert or replace, single valued properties multiple times.
                """
                for i in range (0, 3):
                        # Insert the same single valued properties of music file.
                        self.tracker.update("""
                        INSERT OR REPLACE {
                           <test://instance-1> a nmm:MusicPiece, nfo:FileDataObject;
                           nie:usageCounter '%d';
                           nie:contentAccessed '2000-01-01T00:4%d:47Z' .
                        }""" % (i, i))

                        # Query for the property values and verify whether the last change is applied.
                        result = self.tracker.query ("""
                          SELECT ?playcount ?date WHERE {
                             <test://instance-1> a nmm:MusicPiece ;
                                 nie:usageCounter ?playcount ;
                                 nie:contentAccessed ?date.
                          }""")

                        self.assertEquals (len (result), 1)
                        self.assertEquals (len (result[0]), 2)
                        self.assertEquals (int (result[0][0]), i)
                        self.assertEquals (result[0][1], "2000-01-01T00:4%d:47Z" % (i))

                self.tracker.update ("""
                DELETE { <test://instance-1> a rdfs:Resource. }
                """)

	def test_insert_06(self):
                """
                Insert or replace, single and multi valued properties multiple times.
                """
                for i in range (0, 3):
                        # Insert the same single valued properties and insert multi valued properties at the same time
                        self.tracker.update("""
                        INSERT OR REPLACE {
                           <test://instance-2> a nie:InformationElement;
                           nie:title '%d';
                           nie:keyword '%d'
                        }""" % (i, i))

                        # Query for the property values and verify whether the last change is applied.
                        result = self.tracker.query ("""
                          SELECT ?t ?k WHERE {
                             <test://instance-2> nie:title ?t ;
                                 nie:keyword ?k 
                          }""")

                self.assertEquals (len (result), 3)
                self.assertEquals (len (result[0]), 2)
                self.assertEquals (result[0][0], "%d" % i)
                self.assertEquals (result[0][1], "0")

                self.assertEquals (result[1][0], "%d" % i)
                self.assertEquals (result[1][1], "1")

                self.assertEquals (result[2][0], "%d" % i)
                self.assertEquals (result[2][1], "2")

                self.tracker.update ("""
                DELETE { <test://instance-2> a rdfs:Resource. }
                """)

	def test_insert_07(self):
                """
                Insert or replace, single and multi valued properties with domain errors.
                """

                try:
                  INSERT_SPARQL = """INSERT OR REPLACE { <test://instance-3> nie:title 'test' }"""
                  self.tracker.update (INSERT_SPARQL)
                except:
                  pass

                INSERT_SPARQL = """INSERT OR REPLACE { <test://instance-4> a nie:DataSource }"""
                self.tracker.update (INSERT_SPARQL)

                try:
                  INSERT_SPARQL = """INSERT OR REPLACE { <test://instance-5> nie:rootElementOf <test://instance-4> }"""
                  self.tracker.update (INSERT_SPARQL)
                except:
                  pass

                INSERT_SPARQL = """INSERT OR REPLACE { <test://instance-5> a nie:InformationElement ; nie:rootElementOf <test://instance-4> }"""
                self.tracker.update (INSERT_SPARQL)

                self.tracker.update ("""
                DELETE { <test://instance-4> a rdfs:Resource. }
                """)

                self.tracker.update ("""
                DELETE { <test://instance-5> a rdfs:Resource. }
                """)

	def test_insert_08(self):
                """
                Insert or replace, single and multi valued properties with graphs
                """

                INSERT_SPARQL = """INSERT { GRAPH <test://graph-1> { <test://instance-6> a nie:InformationElement ; nie:title 'title 1' } }"""
                self.tracker.update (INSERT_SPARQL)

                INSERT_SPARQL = """INSERT { GRAPH <test://graph-2> { <test://instance-6> nie:title 'title 1' } }"""
                self.tracker.update (INSERT_SPARQL)

                result = self.tracker.query ("""
                          SELECT ?g ?t WHERE { GRAPH ?g {
                             <test://instance-6> nie:title ?t
                           } }""")

                self.assertEquals (len (result), 1)
                self.assertEquals (len (result[0]), 2)
                self.assertEquals (result[0][0], "test://graph-1") # Yes, indeed
                self.assertEquals (result[0][1], "title 1")

                INSERT_SPARQL = """INSERT OR REPLACE { GRAPH <test://graph-2> { <test://instance-6> nie:title 'title 1' } }"""
                self.tracker.update (INSERT_SPARQL)

                result = self.tracker.query ("""
                          SELECT ?g ?t WHERE { GRAPH ?g {
                             <test://instance-6> nie:title ?t
                           } }""")

                self.assertEquals (len (result), 1)
                self.assertEquals (len (result[0]), 2)
                self.assertEquals (result[0][0], "test://graph-2") # Yup, that's right
                self.assertEquals (result[0][1], "title 1")


                INSERT_SPARQL = """INSERT OR REPLACE { GRAPH <test://graph-3> { <test://instance-6> nie:title 'title 2' } }"""
                self.tracker.update (INSERT_SPARQL)

                result = self.tracker.query ("""
                          SELECT ?g ?t WHERE { GRAPH ?g {
                             <test://instance-6> nie:title ?t
                           } }""")

                self.assertEquals (len (result), 1)
                self.assertEquals (len (result[0]), 2)
                self.assertEquals (result[0][0], "test://graph-3")
                self.assertEquals (result[0][1], "title 2")

                self.tracker.update ("""
                DELETE { <test://instance-6> a rdfs:Resource. }
                """)

        def __insert_valid_date_test (self, datestring, year, month, day, hours, minutes, seconds, timezone):
                """
                Insert a property with datestring value, retrieve its components and validate against
                the expected results (all the other parameters)
                """
                testId = random.randint (10, 1000)
                self.tracker.update ("""
                INSERT {
                   <test://instance-insert-date-%d> a nie:InformationElement;
		        nie:informationElementDate '%s'.
                }
                """ % (testId, datestring))

		result = self.tracker.query ("""
                SELECT    fn:year-from-dateTime (?v)
		          fn:month-from-dateTime (?v)
                          fn:day-from-dateTime (?v)
                          fn:hours-from-dateTime (?v)
                          fn:minutes-from-dateTime (?v)
                          fn:seconds-from-dateTime (?v)
                          fn:timezone-from-dateTime (?v)
		WHERE {
                  <test://instance-insert-date-%d> a nie:InformationElement;
		        nie:informationElementDate ?v .
		}
                 """ % (testId))
                try:
                        self.assertEquals (len (result), 1)
                        self.assertEquals (len (result[0]), 7)
                        self.assertEquals (result[0][0], year)
                        self.assertEquals (result[0][1], month)
                        self.assertEquals (result[0][2], day)
                        self.assertEquals (result[0][3], hours)
                        self.assertEquals (result[0][4], minutes)
                        self.assertEquals (result[0][5], seconds)
                        # FIXME To validate this we need to take into account the locale
                        # self.assertEquals (result[0][7], timezone)
                finally:
                        self.tracker.update ("""
                        DELETE { <test://instance-insert-date-%d> a rdfs:Resource. }
                        """ % (testId))


	"""Date-Time storage testing """
	def test_insert_date_01 (self):
		"""
                1. Insert a InformationElement with date having local timezone info.
                2. TEST: Query and verify the various componentes of date
                """
                self.__insert_valid_date_test ("2004-05-06T13:14:15+0400",
                                               "2004", "05", "06", "13", "14", "15", "14400")


	def test_insert_date_02 (self):
		"""
                1. Insert a InformationElement with date ending with "Z" in TZD.
                2. TEST: Query and verify the various componentes of date
                """
                self.__insert_valid_date_test ("2004-05-06T13:14:15Z",
                                               "2004", "05", "06", "13", "14", "15", "0")

	def test_insert_date_03 (self):
		"""
                1. Insert a InformationElement with date ending with no TZD.
                2. TEST: Query and verify the various componentes of date
                """
                self.__insert_valid_date_test ("2004-05-06T13:14:15",
                                               "2004", "05", "06", "13", "14", "15", "10800") # HEL timezone?


        #@ut.skipIf (1, "It times out in the daemon. Investigate")
	def test_insert_date_04 (self):
		"""
                1. Insert a InformationElement with date having local timezone info
		   with some minutes in it.
                2. TEST: Query and verify the various componentes of date
                """
                self.__insert_valid_date_test ("2004-05-06T13:14:15+0230",
                                               "2004", "05", "06", "13", "14", "15", "9000")


        #@ut.skipIf (1, "It times out in the daemon. Investigate")
        def __test_insert_date_05 (self):
	 	"""
                 1. Insert a InformationElement with date having local timezone info in negative.
                 2. TEST: Query and verify the various componentes of date
                 """
                self.__insert_valid_date_test ("2004-05-06T13:14:15-0230",
                                               "2004", "05", "06", "13", "14", "15", "-9000")


        def __insert_invalid_date_test (self, datestring):
                self.assertRaises (Exception, self.tracker.update, """
                        INSERT {
                           <test://instance-insert-invalid-date-01> a nie:InformationElement;
                              nie:informationElementDate '204-05-06T13:14:15+0400'.
                        }
                        """)

		result = self.tracker.query ("""
                SELECT    fn:year-from-dateTime (?v)
		          fn:month-from-dateTime (?v)
                          fn:day-from-dateTime (?v)
                          fn:hours-from-dateTime (?v)
                          fn:minutes-from-dateTime (?v)
                          fn:seconds-from-dateTime (?v)
                          fn:timezone-from-dateTime (?v)
		WHERE {
                   <test://instances-insert-invalid-date-01> a nie:InformationElement ;
		        nie:informationElementDate ?v .
		}
                """)
                self.assertEquals (len (result), 0)

                #@ut.skipIf (1, "It times out in the daemon. Investigate")
	def test_insert_invalid_date_01 (self):
		"""
                1. Insert a InformationElement with invalid year in date.
                2. TEST: Query and verify the various componentes of date
                """
                self.__insert_invalid_date_test ("204-05-06T13:14:15+0400")


                #@ut.skipIf (1, "It times out in the daemon. Investigate")
	def test_insert_invalid_date_02 (self):
		"""
                1. Insert a InformationElement with date without time.
                2. TEST: Query and verify the various componentes of date
                """
                self.__insert_invalid_date_test ("2004-05-06")



                #@ut.skipIf (1, "It times out in the daemon. Investigate")
	def test_insert_invalid_date_03 (self):
		"""
                1. Insert a InformationElement with date without time but only the "T" separator.
                """
                self.__insert_invalid_date_test ("2004-05-06T")

                #@ut.skipIf (1, "It times out in the daemon. Investigate")
	def test_insert_invalid_date_04 (self):
		"""
                1. Insert a InformationElement with date without time but only the "T" separator.
                """
                self.__insert_invalid_date_test ("2004-05-06T1g:14:15-0200")

	def test_insert_duplicated_url_01 (self):
		"""
                1. Insert a FileDataObject with a known nie:url, twice
                """

		url = "file:///some/magic/path/here"

		insert = """
                INSERT {
                   _:tag a nfo:FileDataObject;
		         nie:url '%s'.
                }
                """ % (url)

		# First insert should go ok
		self.tracker.update (insert)
		# Second insert should not be ok
		try:
			self.tracker.update (insert)
		except Exception:
			pass

		# Only 1 element must be available with the given nie:url
		select = """
                SELECT ?u WHERE { ?u nie:url \"%s\" }
                """ % (url)
		self.assertEquals (len (self.tracker.query (select)), 1)

		# Cleanup
		self.tracker.update ("""
                DELETE { ?u a rdfs:Resource } WHERE { ?u a rdfs:Resource ; nie:url '%s' }
                """ % (url))


	def test_insert_replace_null(self):
                """
                Insert or replace, with null
                """

                self.tracker.update("""INSERT { <test://instance-null> a nie:DataObject, nie:InformationElement }""")
                self.tracker.update("""INSERT { <test://instance-ds1> a nie:DataSource  }""")
                self.tracker.update("""INSERT { <test://instance-ds2> a nie:DataSource  }""")
                self.tracker.update("""INSERT { <test://instance-ds3> a nie:DataSource  }""")
                self.tracker.update("""INSERT { <test://instance-null> nie:dataSource <test://instance-ds1>, <test://instance-ds2>, <test://instance-ds3> }""")

                # null upfront, reset of list, rewrite of new list
                self.tracker.update("""INSERT OR REPLACE { <test://instance-null> nie:dataSource null, <test://instance-ds1>, <test://instance-ds2> }""")
                result = self.tracker.query ("""SELECT ?ds WHERE { <test://instance-null> nie:dataSource ?ds }""")
                self.assertEquals (len (result), 2)
                self.assertEquals (len (result[0]), 1)
                self.assertEquals (len (result[1]), 1)
                self.assertEquals (result[0][0], "test://instance-ds1")
                self.assertEquals (result[1][0], "test://instance-ds2")

                # null upfront, reset of list, rewrite of new list, second test
                self.tracker.update("""INSERT OR REPLACE { <test://instance-null> nie:dataSource null, <test://instance-ds1>, <test://instance-ds2>, <test://instance-ds3> }""")
                result = self.tracker.query ("""SELECT ?ds WHERE { <test://instance-null> nie:dataSource ?ds }""")
                self.assertEquals (len (result), 3)
                self.assertEquals (len (result[0]), 1)
                self.assertEquals (len (result[1]), 1)
                self.assertEquals (len (result[2]), 1)
                self.assertEquals (result[0][0], "test://instance-ds1")
                self.assertEquals (result[1][0], "test://instance-ds2")
                self.assertEquals (result[2][0], "test://instance-ds3")

                # null in the middle, rewrite of new list
                self.tracker.update("""INSERT OR REPLACE { <test://instance-null> nie:dataSource <test://instance-ds1>, null, <test://instance-ds2>, <test://instance-ds3> }""")
                result = self.tracker.query ("""SELECT ?ds WHERE { <test://instance-null> nie:dataSource ?ds }""")
                self.assertEquals (len (result), 2)
                self.assertEquals (len (result[0]), 1)
                self.assertEquals (len (result[1]), 1)
                self.assertEquals (result[0][0], "test://instance-ds2")
                self.assertEquals (result[1][0], "test://instance-ds3")
				
                # null at the end
                self.tracker.update("""INSERT OR REPLACE { <test://instance-null> nie:dataSource <test://instance-ds1>, <test://instance-ds2>, <test://instance-ds3>, null }""")
                result = self.tracker.query ("""SELECT ?ds WHERE { <test://instance-null> nie:dataSource ?ds }""")
                self.assertEquals (len (result), 0)
	
                # Multiple nulls
                self.tracker.update("""INSERT OR REPLACE { <test://instance-null> nie:dataSource null, <test://instance-ds1>, null, <test://instance-ds2>, <test://instance-ds3> }""")
                result = self.tracker.query ("""SELECT ?ds WHERE { <test://instance-null> nie:dataSource ?ds }""")
                self.assertEquals (len (result), 2)
                self.assertEquals (len (result[0]), 1)
                self.assertEquals (len (result[1]), 1)
                self.assertEquals (result[0][0], "test://instance-ds2")
                self.assertEquals (result[1][0], "test://instance-ds3")
	
                self.tracker.update ("""DELETE { <test://instance-null> a rdfs:Resource. }""")
                self.tracker.update ("""DELETE { <test://instance-ds1> a rdfs:Resource. }""")
                self.tracker.update ("""DELETE { <test://instance-ds2> a rdfs:Resource. }""")
                self.tracker.update ("""DELETE { <test://instance-ds3> a rdfs:Resource. }""")

class TrackerStoreDeleteTests (CommonTrackerStoreTest):
        """
        Use DELETE in Sparql and check the information is actually removed
        """
        def test_delete_01 (self):
                """
                Insert triples and Delete a triple. Verify the deletion with a query
                """

		# first insert
                self.tracker.update ("""
                INSERT {
                   <urn:uuid:7646001> a nco:Contact;
                            nco:fullname 'Artist_1_delete'.
                   <test://instance-test-delete-01> a nmm:MusicPiece, nfo:FileDataObject;
                            nfo:fileName '11_song_del.mp3';
                            nfo:genre 'Classic delete';
                            nmm:musicAlbum '1_Album_delete';
                            nmm:performer <urn:uuid:7646001>.
                }
                """)

		# verify the insertion
                result = self.tracker.query ("""
                SELECT ?u WHERE {
                    ?u a nmm:MusicPiece ;
                         nfo:genre 'Classic delete' .
                }
                """)
                self.assertEquals (len (result), 1)
                self.assertEquals (len (result[0]), 1)
                self.assertEquals (result[0][0], "test://instance-test-delete-01")

		# now delete
                self.tracker.update("""
                DELETE {
                  <test://instance-test-delete-01> a rdfs:Resource.
                }
                """)

                # Check the instance is not there
                result = self.tracker.query ("""
                SELECT ?u WHERE {
                    ?u a nmm:MusicPiece ;
                         nfo:genre 'Classic delete' .
                }
                """)
                self.assertEquals (len (result), 0)


	def test_delete_02 (self):
                """
                Delete a MusicAlbum and count the album

		1. add a music album.
		2. count the number of albums
		3. delete an album
		2. count the number of albums
		"""

                initial = self.tracker.count_instances ("nmm:MusicAlbum")

		"""Add a music album """
                self.tracker.update ("""
                INSERT {
                   <test://instance-delete-02> a nmm:MusicAlbum;
                           nie:title '06_Album_delete'.
                }
                """)

                after_insert = self.tracker.count_instances ("nmm:MusicAlbum")
                self.assertEquals (initial+1, after_insert)

		"""Delete the added music album """
                self.tracker.update("""
                DELETE {
                  <test://instance-delete-02> a nmm:MusicAlbum.
                }
                """)

		"""get the count of music albums"""
                after_removal = self.tracker.count_instances ("nmm:MusicAlbum")

                self.assertEquals (after_removal, initial)


class TrackerStoreBatchUpdateTest (CommonTrackerStoreTest):
        """
        Insert data using the BatchSparqlUpdate method in the store
        """

	def test_batch_insert_01(self):
		"""
                batch insertion of 100 contacts:
		1. insert 100 contacts.
		2. delete the inserted contacts.
		"""
                NUMBER_OF_TEST_CONTACTS = 3

		# query no. of existing contacts. (predefined instances in the DB)
		count_before_insert = self.tracker.count_instances ("nco:PersonContact")

		# insert contacts.
                CONTACT_TEMPLATE = """
                   <test://instance-contact-%d> a nco:PersonContact ;
                      nco:nameGiven 'Contact-name %d';
                      nco:nameFamily 'Contact-family %d';
                      nie:generator 'test-instance-to-remove' ;
                      nco:contactUID '%d';
                      nco:hasPhoneNumber <tel:%s> .
                """

                global contact_list
                contact_list = []
                def complete_contact (contact_template):
                        random_phone = "".join ([str(random.randint (0, 9)) for i in range (0, 9)])
                        contact_counter = random.randint (0, 10000)

                        # Avoid duplicates
                        while contact_counter in contact_list:
                                contact_counter = random.randint (0, 10000)
                        contact_list.append (contact_counter)

                        return contact_template % (contact_counter,
                                                   contact_counter,
                                                   contact_counter,
                                                   contact_counter,
                                                   random_phone)

                contacts = map (complete_contact, [CONTACT_TEMPLATE] * NUMBER_OF_TEST_CONTACTS)
		INSERT_SPARQL = "\n".join (["INSERT {"] + contacts +["}"])
       		self.tracker.batch_update (INSERT_SPARQL)

		# Check all instances are in
		count_after_insert = self.tracker.count_instances ("nco:PersonContact")
                self.assertEquals (count_before_insert + NUMBER_OF_TEST_CONTACTS, count_after_insert)

		""" Delete the inserted contacts """
                DELETE_SPARQL = """
                DELETE {
                  ?x a rdfs:Resource .
                } WHERE {
                  ?x a nco:PersonContact ;
                      nie:generator 'test-instance-to-remove' .
                }
                """
                self.tracker.update (DELETE_SPARQL)
                count_final = self.tracker.count_instances ("nco:PersonContact")
                self.assertEquals (count_before_insert, count_final)

class TrackerStorePhoneNumberTest (CommonTrackerStoreTest):
	"""
        Tests around phone numbers (maemo specific). Inserting correct/incorrect ones
        and running query to get the contact from the number.
	"""

 	def test_phone_01 (self):
		"""
                1. Setting the maemo:localPhoneNumber property to last 7 digits of phone number.
		2. Receiving a message  from a contact whose localPhoneNumber is saved.
		3. Query messages from the local phone number
		"""
		PhoneNumber = str(random.randint (0, sys.maxint))
		UUID	    = str(time.time())
		UUID1	    = str(random.randint (0, sys.maxint))
		UUID2	    = str(random.randint (0, sys.maxint))
		localNumber = PhoneNumber[-7:]
		d=datetime.datetime.now()
	        Received=d.isoformat()
		ID	    = int(time.time())%1000
		Given_Name  = 'test_GN_' + `ID`
		Family_Name = 'test_FN_' + `ID`

		INSERT_CONTACT_PHONE = """
                INSERT {
                    <tel:123456789> a nco:PhoneNumber ;
                          nco:phoneNumber  '00358555444333' ;
                          maemo:localPhoneNumber '5444333'.

                    <test://test_phone_1/contact> a nco:PersonContact;
			nco:contactUID '112';
                        nco:nameFamily 'Family-name'  ;
                        nco:nameGiven 'Given-name'.
                    <test://test_phone_1/contact> nco:hasPhoneNumber <tel:123456789>.
                }
                """
                self.tracker.update (INSERT_CONTACT_PHONE)

		INSERT_MESSAGE = """
                INSERT {
                    <test://test_phone_1/message> a nmo:Message ;
                         nmo:from [a nco:Contact ; nco:hasPhoneNumber <tel:123456789>];
                         nmo:receivedDate '2010-01-02T10:13:00Z' ;
                         nie:plainTextContent 'hello'
                }
                """
                self.tracker.update (INSERT_MESSAGE)

		QUERY_SPARQL = """
                SELECT ?msg WHERE {
                     ?msg a nmo:Message;
                         nmo:from ?c .
                     ?c nco:hasPhoneNumber ?n .
                     ?n maemo:localPhoneNumber '5444333'.
		} """
                result = self.tracker.query (QUERY_SPARQL)
                self.assertEquals (len (result), 1)
                self.assertEquals (len (result[0]), 1)
                self.assertEquals (result[0][0], "test://test_phone_1/message")


	def test_phone_02 (self):
		"""
                Inserting a local phone number which have spaces
                """
		INSERT_SPARQL = """
                INSERT {
			<tel+3333333333> a nco:PhoneNumber ;
				nco:phoneNumber  <tel+3333333333> ;
                                maemo:localPhoneNumber '333 333'.

			<test://test_phone_02/contact> a nco:PersonContact;
				nco:nameFamily 'test_name_01' ;
				nco:nameGiven 'test_name_02';
                                nco:hasPhoneNumber <tel+3333333333> .
                }
                """
                self.assertRaises (Exception, self.tracker.update (INSERT_SPARQL))


if __name__ == "__main__":
	ut.main()
