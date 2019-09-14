using GLib;
using Tracker;
using Tracker.Sparql;

const string insert_query_replace = "
DELETE { ?r nao:hasTag ?tag . }
WHERE { 
	?r a nco:PhoneNumber;
	   nco:phoneNumber \"02141730585%d\";
	   nao:hasTag ?tag .
}

DELETE {
	?r a nco:CarPhoneNumber, nco:BbsNumber, nco:PagerNumber,
	     nco:VideoTelephoneNumber, nco:MessagingNumber, nco:VoicePhoneNumber,
	     nco:CellPhoneNumber, nco:FaxNumber, nco:ModemNumber .
} WHERE {
	?r a nco:PhoneNumber;
	   nco:phoneNumber \"02141730585%d\" .
}

INSERT {
	_:tel a nco:PhoneNumber ;
	      nco:phoneNumber \"02141730585%d\" .
} WHERE {
	OPTIONAL {
		?r a nco:PhoneNumber;
		   nco:phoneNumber \"02141730585%d\" .
	}
	FILTER(!bound(?r)) .
}

INSERT OR REPLACE { <mailto:rhome0@example.com%d> a nco:EmailAddress ;
		nco:emailAddress \"rhome0@example.com%d\" . }

DELETE { <contact:r:%d> nco:hasAffiliation ?e . ?e a rdfs:Resource }
WHERE { <contact:r:%d> a nco:PersonContact ; nco:hasAffiliation ?e }

INSERT OR REPLACE {
	_:af1 a nco:Affiliation ;
	      rdfs:label \"Work\" ;
	      nco:hasEmailAddress <mailto:rhome0@example.com%d> .

	_:af2 a nco:Affiliation ;
	      rdfs:label \"Other\" ;
	      nco:hasPhoneNumber ?tel .

	<contact:r:%d> a nco:PersonContact ;
	               nco:nameGiven \"First %d\" ;
	               nco:nameFamily \"Last %d\" ;
	               nco:hasAffiliation _:af1 ;
	               nco:hasAffiliation _:af2 ;
	               nie:contentCreated \"2011-03-14T13:47:25\" ;
	               nco:contactUID \"c1f1b12d-bc75-4d45-9a1f-b1efe934409f\" .
} WHERE {
	?tel nco:phoneNumber \"02141730585%d\"
}";

const string insert_query_original = "
DELETE { ?r nao:tag ?tag . }
WHERE { 
	?r a nco:PhoneNumber; nco:phoneNumber \"2141730585%d\";
	   nao:hasTag ?tag .
}

DELETE {
	?r a nco:CarPhoneNumber, nco:BbsNumber, nco:PagerNumber,
	     nco:VideoTelephoneNumber, nco:MessagingNumber, nco:VoicePhoneNumber,
	     nco:CellPhoneNumber, nco:FaxNumber, nco:ModemNumber .
} WHERE {
	?r a nco:PhoneNumber;
	   nco:phoneNumber \"2141730585%d\" .
}

INSERT {
	_:tel a nco:PhoneNumber ;
	      nco:phoneNumber \"2141730585%d\" .
} WHERE {
	OPTIONAL {
		?r a nco:PhoneNumber;
		   nco:phoneNumber \"2141730585%d\" .
	}
	FILTER(!bound(?r)) .
}

INSERT { <mailto:home0@example.com%d> a nco:EmailAddress ;
			nco:emailAddress \"home0@example.com%d\" . }

DELETE { <contact:o:%d> nco:hasAffiliation ?e . ?e a rdfs:Resource }
WHERE { <contact:o:%d> a nco:PersonContact ; nco:hasAffiliation ?e }

DELETE { GRAPH <urn:uuid:08070f5c-a334-4d19-a8b0-12a3071bfab9> {
    <contact:o:%d> ?predicate ?object .
} } WHERE { GRAPH <urn:uuid:08070f5c-a334-4d19-a8b0-12a3071bfab9> {
    <contact:o:%d> ?predicate ?object .
    FILTER(?predicate NOT IN (nco:contactLocalUID,nco:contactUID,rdf:type)) .
} }

INSERT { GRAPH <urn:uuid:08070f5c-a334-4d19-a8b0-12a3071bfab9> {
	_:af1 a nco:Affiliation ;
	      rdfs:label \"Work\" ;
	      nco:hasEmailAddress <mailto:home0@example.com%d> .

	_:af2 a nco:Affiliation ;
	      rdfs:label \"Other\" ;
	      nco:hasPhoneNumber ?tel .

	<contact:o:%d> a nco:PersonContact ;
	               nco:nameGiven \"First %d\" ;
	               nco:nameFamily \"Last %d\" ;
	               nco:hasAffiliation _:af1 ;
	               nco:hasAffiliation _:af2 ;
	               nie:contentCreated \"2011-03-14T13:47:25\" ;
	               nco:contactUID \"c1f1b12d-bc75-4d45-9a1f-b1efe934409f\" .
} } WHERE {
	?tel nco:phoneNumber \"2141730585%d\"
}";


int main (string[] args) {
	try {
		uint i, y = 100;
		Timer timer = new Timer ();
		Connection c;
		c = Connection.get ();

		if (args.length == 3) {
			y = int.parse (args[2]);
		}

		if (args.length == 1 || args[1] == "replace") {
			timer.start ();
			for (i = 0; i < y; i++) {
				c.update (insert_query_replace.printf (i,i,i,i,i,i,i,i,i,i,i,i,i));
			}
			timer.stop ();

			print ("REPLACE  : %u contacts: %f\n", y, timer.elapsed());
		}


		if (args.length == 1 || args[1] == "original") {
			timer.start ();
			for (i = 0; i < y; i++) {
				c.update (insert_query_original.printf (i,i,i,i,i,i,i,i,i,i,i,i,i,i,i));
			}
			timer.stop ();

			print ("ORIGINAL : %u contacts: %f\n", y, timer.elapsed());
		}
	} catch (GLib.Error e) {
		critical ("%s", e.message);
	}

	return 0;
}
