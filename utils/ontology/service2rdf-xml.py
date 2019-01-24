#
# Copyright (C) 2009, Nokia <ivan.frade@nokia.com>
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

import configparser, os
import sys
import getopt

def usage():
  print("Usage: python service2rdf-xml.py --metadata=ONTOLOGY.metadata --service=ONTOLOGY.service [--uri=URI]")

def main():
  try:
     uri = ""
     metadataf = ""
     servicef = ""

     opts, args = getopt.getopt(sys.argv[1:], "hu:va:vm:vs:v", ["help", "uri=", "metadata=", "service="])

     for o, a in opts:
         if o in ("-u", "--uri"):
           uri = a
         elif o in ("-m", "--metadata"):
           metadataf = a
         elif o in ("-s", "--service"):
           servicef = a
         elif o in ("-h", "--help"):
           usage ()
           sys.exit()

     if uri == "":
       uri = "http://live.gnome.org/Tracker/XMLSchema"

     if metadataf == "" or servicef == "":
       usage ()
       sys.exit ()

     service = configparser.ConfigParser()
     service.readfp(open(servicef))

     metadata = configparser.ConfigParser()
     metadata.readfp(open(metadataf))

     print("<rdf:RDF")
     print("  xmlns:nid3=\"http://www.semanticdesktop.org/ontologies/2007/05/10/nid3#\"")
     print("  xmlns:nfo=\"http://www.semanticdesktop.org/ontologies/2007/03/22/nfo#\"")
     print("  xmlns:nmo=\"http://www.semanticdesktop.org/ontologies/2007/03/22/nmo#\"")
     print("  xmlns:nie=\"http://www.semanticdesktop.org/ontologies/2007/01/19/nie#\"")
     print("  xmlns:exif=\"http://www.kanzaki.com/ns/exif#\"")
     print("  xmlns:nao=\"http://www.semanticdesktop.org/ontologies/2007/08/15/nao#\"")
     print("  xmlns:rdfs=\"http://www.w3.org/2000/01/rdf-schema#\"")
     print("  xmlns:protege=\"http://protege.stanford.edu/system#\"")
     print("  xmlns:dcterms=\"http://purl.org/dc/terms/\"")
     print("  xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\"")
     print("  xmlns:ncal=\"http://www.semanticdesktop.org/ontologies/2007/04/02/ncal#\"")
     print("  xmlns:xsd=\"http://www.w3.org/2001/XMLSchema#\"")
     print("  xmlns:nrl=\"http://www.semanticdesktop.org/ontologies/2007/08/15/nrl#\"")
     print("  xmlns:pimo=\"http://www.semanticdesktop.org/ontologies/2007/11/01/pimo#\"")
     print("  xmlns:geo=\"http://www.w3.org/2003/01/geo/wgs84_pos#\"")
     print("  xmlns:tmo=\"http://www.semanticdesktop.org/ontologies/2008/05/20/tmo#\"")
     print("  xmlns:dc=\"http://purl.org/dc/elements/1.1/\"")
     print("  xmlns:nco=\"http://www.semanticdesktop.org/ontologies/2007/03/22/nco#\"")
     print("  xmlns:nexif=\"http://www.semanticdesktop.org/ontologies/2007/05/10/nexif#\">")

     print("")

     for klass in service.sections():
        splitted = klass.split (":")
        print("\t<rdfs:Class rdf:about=\"" + uri + "/" + splitted[0] + "#" + splitted[1] + "\">")
        print("\t\t<rdfs:label>" + splitted[1] + "</rdfs:label>")

        for name, value in service.items (klass):
           if name == "SuperClasses":
             vsplit = value.split (";")
             for val in vsplit:
               vvsplit = val.split (":");
               print("\t\t<rdfs:subClassOf>")
               print("\t\t\t<rdfs:Class rdf:about=\"" + uri + "/" +vvsplit[0] + "#" + vvsplit[1] + "\"/>")
               print("\t\t</rdfs:subClassOf>")
        print("\t</rdfs:Class>")

     for mdata in metadata.sections():
        splitted = mdata.split (":")
        print("\t<rdf:Property rdf:about=\"" + uri + "#" + splitted[1] + "\">")
        print("\t\t<rdfs:label>" + splitted[1] + "</rdfs:label>")

        for name, value in metadata.items (mdata):
           if name == "datatype":
             print("\t\t<rdfs:range rdf:resource=\"" + uri + "#" + value + "\"/>")

           if name == "domain":
            vvsplit = value.split (":")
            print("\t\t<rdfs:domain rdf:resource=\"" + uri + "/" +vvsplit[0] + "#" + vvsplit[1] + "\"/>")

           if name == "parent":
            print("\t\t<rdfs:subPropertyOf rdf:resource=\"" + uri + "#" + value.split (":")[1] + "\"/>")

           if name == "weight":
            print("\t\t<rdfs:comment>Weight is " + value + "</rdfs:comment>")

        print("\t</rdf:Property>")

     print("</rdf:RDF>")
  except getopt.GetoptError as err:
     print(str(err))
     usage ()
     sys.exit(2)

if __name__ == "__main__":
    main()

