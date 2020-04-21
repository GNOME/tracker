<xsl:stylesheet version = '1.0'
     xmlns:xsl='http://www.w3.org/1999/XSL/Transform'
  xmlns:nid3="http://tracker.api.gnome.org/ontology/v3/nid3#"
  xmlns:nfo="http://tracker.api.gnome.org/ontology/v3/nfo#"
  xmlns:nmo="http://tracker.api.gnome.org/ontology/v3/nmo#"
  xmlns:nie="http://tracker.api.gnome.org/ontology/v3/nie#"
  xmlns:exif="http://www.kanzaki.com/ns/exif#"
  xmlns:nao="http://tracker.api.gnome.org/ontology/v3/nao#"
  xmlns:rdfs="http://www.w3.org/2000/01/rdf-schema#"
  xmlns:protege="http://protege.stanford.edu/system#"
  xmlns:dcterms="http://purl.org/dc/terms/"
  xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#"
  xmlns:ncal="http://tracker.api.gnome.org/ontology/v3/ncal#"
  xmlns:xsd="http://www.w3.org/2001/XMLSchema#"
  xmlns:nrl="http://tracker.api.gnome.org/ontology/v3/nrl#"
  xmlns:pimo="http://www.semanticdesktop.org/ontologies/2007/11/01/pimo#"
  xmlns:geo="http://www.w3.org/2003/01/geo/wgs84_pos#"
  xmlns:tmo="http://www.semanticdesktop.org/ontologies/2008/05/20/tmo#"
  xmlns:dc="http://purl.org/dc/elements/1.1/"
  xmlns:nco="http://tracker.api.gnome.org/ontology/v3/nco#"
  xmlns:nexif="http://www.semanticdesktop.org/ontologies/2007/05/10/nexif#">
<xsl:output method="text" />

<xsl:template name="predicate-of">
 <xsl:param name="about"/>
 <xsl:choose>
  <xsl:when test="substring-after($about, '/')">
   <xsl:call-template name="predicate-of">
     <xsl:with-param name="about" select="substring-after($about, '/')"/>
   </xsl:call-template>
  </xsl:when>
  <xsl:otherwise>
     <xsl:choose>
       <xsl:when test="substring-after($about, '#')">
         <xsl:if test="substring-before($about, '#') != 'XMLSchema'"><xsl:value-of select="substring-before($about, '#')"/>:</xsl:if><xsl:value-of select="substring-after($about, '#')"/>
       </xsl:when>
       <xsl:otherwise>DC:<xsl:value-of select="$about"/></xsl:otherwise>
    </xsl:choose>
  </xsl:otherwise>
 </xsl:choose>
</xsl:template>
<xsl:template match="rdf:RDF">
@prefix rdf: &lt;http://www.w3.org/1999/02/22-rdf-syntax-ns#&gt; .
@prefix foaf: &lt;http://xmlns.com/foaf/0.1/&gt; .
@prefix owl: &lt;http://www.w3.org/2002/07/owl#&gt; .
@prefix xsl: &lt;http://www.w3.org/1999/XSL/Transform#&gt; .
@prefix nid3: &lt;http://tracker.api.gnome.org/ontology/v3/nid3#&gt; .
@prefix nfo: &lt;http://tracker.api.gnome.org/ontology/v3/nfo#&gt; .
@prefix nmo: &lt;http://tracker.api.gnome.org/ontology/v3/nmo#&gt; .
@prefix nie: &lt;http://tracker.api.gnome.org/ontology/v3/nie#&gt; .
@prefix exif: &lt;http://www.kanzaki.com/ns/exif#&gt; .
@prefix nao: &lt;http://tracker.api.gnome.org/ontology/v3/nao#&gt; .
@prefix rdfs: &lt;http://www.w3.org/2000/01/rdf-schema#&gt; .
@prefix protege: &lt;http://protege.stanford.edu/system#&gt; .
@prefix dcterms: &lt;http://purl.org/dc/terms/&gt; .
@prefix ncal: &lt;http://tracker.api.gnome.org/ontology/v3/ncal#&gt; .
@prefix xsd: &lt;http://www.w3.org/2001/XMLSchema#&gt; .
@prefix nrl: &lt;http://tracker.api.gnome.org/ontology/v3/nrl#&gt; .
@prefix pimo: &lt;http://www.semanticdesktop.org/ontologies/2007/11/01/pimo#&gt; .
@prefix geo: &lt;http://www.w3.org/2003/01/geo/wgs84_pos#&gt; .
@prefix tmo: &lt;http://www.semanticdesktop.org/ontologies/2008/05/20/tmo#&gt; .
@prefix dc: &lt;http://purl.org/dc/elements/1.1/&gt; .
@prefix nco: &lt;http://tracker.api.gnome.org/ontology/v3/nco#&gt; .
@prefix nexif: &lt;http://www.semanticdesktop.org/ontologies/2007/05/10/nexif#&gt; .

<xsl:for-each select="rdfs:Class">
<xsl:variable name="about" select="@rdf:about"/>
<xsl:for-each select="/rdf:RDF/rdf:Property/rdfs:domain[@rdf:resource=$about]">
<xsl:choose>
<xsl:when test="substring-after(../rdfs:range/@rdf:resource, '#') = 'string'">

# Query for <xsl:call-template name="predicate-of"><xsl:with-param name="about"><xsl:value-of select="../@rdf:about"/></xsl:with-param></xsl:call-template>
SELECT ?<xsl:value-of select="substring-after(../@rdf:about, '#')"/>
WHERE { ?x <xsl:call-template name="predicate-of"><xsl:with-param name="about"><xsl:value-of select="../@rdf:about"/></xsl:with-param></xsl:call-template> ?<xsl:value-of select="substring-after(../@rdf:about, '#')"/> }
ORDER BY ?<xsl:value-of select="substring-after(../@rdf:about, '#')"/>
</xsl:when>
<xsl:otherwise>
</xsl:otherwise>
</xsl:choose>

</xsl:for-each>

</xsl:for-each>
</xsl:template>
</xsl:stylesheet>
