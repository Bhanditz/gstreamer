<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">

<xsl:output method="text" encoding="us-ascii" omit-xml-declaration="yes" indent="no"/>

 <xsl:template match="/element">
  <xsl:apply-templates select="name"/>
  <xsl:apply-templates select="details"/>
  <xsl:apply-templates select="object"/>
  <xsl:apply-templates select="pad-templates"/>
  <xsl:apply-templates select="element-flags"/>
  <xsl:apply-templates select="element-implementation"/>
  <xsl:apply-templates select="clocking-interaction"/>
  <xsl:apply-templates select="pads"/>
  <xsl:apply-templates select="element-properties"/>
  <xsl:apply-templates select="element-signals"/>
  <xsl:apply-templates select="element-actions"/>
 </xsl:template>

 <xsl:template match="name">
  <xsl:text>Element Name: </xsl:text><xsl:value-of select="."/>
  <xsl:text>&#10;&#10;</xsl:text>
 </xsl:template>

 <xsl:template match="details">
  <xsl:text>Factory Details:&#10;</xsl:text> 
  <xsl:text>  Long Name:&#9;</xsl:text>   <xsl:value-of select="long-name"/>   <xsl:text>&#10;</xsl:text>
  <xsl:text>  Class:&#9;</xsl:text>       <xsl:value-of select="class"/>       <xsl:text>&#10;</xsl:text>
  <xsl:text>  License:&#9;</xsl:text>     <xsl:value-of select="license"/>     <xsl:text>&#10;</xsl:text>
  <xsl:text>  Description:&#9;</xsl:text> <xsl:value-of select="description"/> <xsl:text>&#10;</xsl:text>
  <xsl:text>  Version:&#9;</xsl:text>     <xsl:value-of select="version"/>     <xsl:text>&#10;</xsl:text>
  <xsl:text>  Author(s):&#9;</xsl:text>   <xsl:value-of select="authors"/>     <xsl:text>&#10;</xsl:text>
  <xsl:text>  Copyright:&#9;</xsl:text>   <xsl:value-of select="copyright"/>   <xsl:text>&#10;</xsl:text>
  <xsl:text>&#10;</xsl:text>
 </xsl:template>

 <xsl:template match="object">
 </xsl:template>

 <xsl:template match="pad-templates">
  <xsl:text>Pad Templates&#10;</xsl:text>
  <xsl:apply-templates select="./pad-template"/>
 </xsl:template>

 <xsl:template match="pad-template">
  <xsl:text>  </xsl:text>
  <xsl:value-of select="direction"/> 
  <xsl:text> template: </xsl:text>
  <xsl:value-of select="name"/>
  <xsl:text>&#10;</xsl:text>
  <xsl:text>    Availability: </xsl:text> <xsl:value-of select="presence"/>
  <xsl:text>&#10;</xsl:text>
  <xsl:text>    Capabilities:&#10; </xsl:text> <xsl:apply-templates select="./capscomp"/>
 </xsl:template>

 <xsl:template match="capscomp">
  <xsl:apply-templates select="./caps"/>
  <xsl:text>&#10;</xsl:text>
 </xsl:template>

 <xsl:template match="caps">
  <xsl:text>     '</xsl:text>
  <xsl:value-of select="name"/>
  <xsl:text>'&#10;</xsl:text>
  <xsl:text>        MIME type: </xsl:text>
  <xsl:value-of select="type"/>
  <xsl:text>'&#10;</xsl:text>
  <xsl:apply-templates select="./properties"/>
 </xsl:template>

 <xsl:template match="properties">
  <xsl:apply-templates select="*"/>
 </xsl:template>

 <xsl:template match="list">
  <xsl:text>        </xsl:text>
  <xsl:value-of select="@name"/>
  <xsl:text>&#9;:List&#10;</xsl:text>
  <xsl:apply-templates select="*" mode="list"/>
 </xsl:template>

 <!-- propety entries in list mode -->
 <xsl:template match="string" mode="list">
  <xsl:text>         String: '</xsl:text>
  <xsl:value-of select="@value"/>
  <xsl:text>'&#10;</xsl:text>
 </xsl:template>
 
 <xsl:template match="fourcc" mode="list">
  <xsl:text>         FourCC: '</xsl:text>
  <xsl:value-of select="@hexvalue"/>
  <xsl:text>'&#10;</xsl:text>
 </xsl:template>

 <xsl:template match="int" mode="list">
  <xsl:text>         Integer: </xsl:text>
  <xsl:value-of select="@value"/>
  <xsl:text>&#10;</xsl:text>
 </xsl:template>

 <xsl:template match="range" mode="list">
  <xsl:text>         Integer range: </xsl:text>
  <xsl:value-of select="concat(@min, ' - ', @max)"/> 
  <xsl:text>&#10;</xsl:text>
 </xsl:template>

 <!-- propety entries in normal mode -->
 <xsl:template match="string">
  <xsl:text>         </xsl:text>
  <xsl:value-of select="@name"/>
  <xsl:text>     : String: '</xsl:text>
  <xsl:value-of select="@value"/> 
  <xsl:text>'&#10;</xsl:text>
 </xsl:template>

 <xsl:template match="fourcc">
  <xsl:text>         </xsl:text>
  <xsl:value-of select="@name"/>
  <xsl:text>     : FourCC: '</xsl:text>
  <xsl:value-of select="@hexvalue"/> 
  <xsl:text>'&#10;</xsl:text>
 </xsl:template>

 <xsl:template match="int">
  <xsl:text>         </xsl:text>
  <xsl:value-of select="@name"/>
  <xsl:text>     : Integer: </xsl:text>
  <xsl:value-of select="@value"/> 
  <xsl:text>&#10;</xsl:text>
 </xsl:template>

 <xsl:template match="range"> 		
  <xsl:text>         </xsl:text>
  <xsl:value-of select="@name"/>
  <xsl:text>     : Integer range: </xsl:text>
  <xsl:value-of select="concat(@min, ' - ', @max)"/> 
  <xsl:text>&#10;</xsl:text>
 </xsl:template>

 <xsl:template match="flag">
  <xsl:text>  </xsl:text>
  <xsl:value-of select="."/>
  <xsl:text>&#10;</xsl:text>
 </xsl:template>

 <xsl:template match="element-flags">
  <xsl:text>Element Flags:&#10;</xsl:text>
  <xsl:apply-templates select="./flag"/>
  <xsl:text>&#10;</xsl:text>
 </xsl:template>

 <xsl:template match="state-change">
  <xsl:text>  Has change_state() function: </xsl:text>
  <xsl:value-of select="@function"/>
  <xsl:text>&#10;</xsl:text>
 </xsl:template>

 <xsl:template match="load">
  <xsl:text>  Has custom restore_thyself() function: </xsl:text>
  <xsl:value-of select="@function"/>
  <xsl:text>&#10;</xsl:text>
 </xsl:template>

 <xsl:template match="save">
  <xsl:text>  Has custom save_thyself() function: </xsl:text>
  <xsl:value-of select="@function"/>
  <xsl:text>&#10;</xsl:text>
 </xsl:template>

 <xsl:template match="element-implementation">
  <xsl:text>Element Implementation:&#10;</xsl:text>
  <xsl:apply-templates select="*"/>
  <xsl:text>&#10;</xsl:text>
 </xsl:template>

 <xsl:template match="clocking-interaction">
  <xsl:text>Clocking Interaction:&#10;</xsl:text>
  <xsl:text>&#10;</xsl:text>
 </xsl:template>

 <xsl:template match="pads">
  <xsl:text>Pads:&#10;</xsl:text>
  <xsl:text>&#10;</xsl:text>
 </xsl:template>

 <xsl:template match="element-properties">
  <xsl:text>Element Arguments:&#10;</xsl:text>
  <xsl:text>&#10;</xsl:text>
 </xsl:template>

 <xsl:template match="element-signals">
  <xsl:text>Element Signals:&#10;</xsl:text>
  <xsl:text>&#10;</xsl:text>
 </xsl:template>

 <xsl:template match="element-actions">
  <xsl:text>Element Actions:&#10;</xsl:text>
  <xsl:text>&#10;</xsl:text>
 </xsl:template>

</xsl:stylesheet>
