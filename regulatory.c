#include <stdio.h>
#include <strings.h>
#include <string.h>
#include <stdlib.h>

// Get list of country names and abbreviations
#include "countries.h"

int append_string(char *out,int *offset,int max_len,char *piece)
{
  if (strlen(piece)+(*offset)>=max_len) {
    fprintf(stderr,"ERROR: Buffer would overflow adding '%s'\n",piece);
    exit(-1);
  }
  strcpy(&out[*offset],piece); (*offset)+=strlen(piece);
  return 0;
}

int mark_country(char *country,int *country_flags)
{
  for(int i=0;countries[i].country;i++)
    if (!strncasecmp(country,countries[i].iso3166_code,2)) {
      country_flags[i]=1;
      return 0;
    }
  fprintf(stderr,"WARNING: ISO3166 country code '%c%c' unknown.\n",
	  country[0],country[1]);
  return -1;
}

int generate_regulatory_information(char *out,int max_len,char *primary_country,
				    char *all_countries,
				    int frequency, int max_txpower,
				    int duty_cycle)
{
  // XXX - Generate regulatory information from the supplied information.
  // XXX - Would be good to provide a warning if the data provided is not
  // consistent, e.g., frequency does not match with what we know about the
  // countries in the list.

  int offset=0;
  char piece[1024];

  int country_flags[1024];
  for(int i=0;i<1024;i++) country_flags[i]=0;
  mark_country(primary_country,country_flags);
  for(int i=0;i<strlen(all_countries);i+=3) {
    mark_country(&all_countries[i],country_flags);
  }
  
  
  append_string(out,&offset,max_len,
		"<p class=warning>This Mesh Extender has been configured with the intention of"
		" operation in the following locations.  Note that this does not"
		" constitute a legal opinion, indeminification or anything similar,"
		" and all operation and use of this Mesh Extender is the"
		" responsibility of its operator.\n\n"
		"<p class=locationlistheading>The list of locations follows, and in alphabetical order, and using the conventions of ISO3166:\n\n"
		"<table class=locationlisttable>\n");
  
  for(int i=0;i<1024;i++)
    if (country_flags[i]) {
      snprintf(piece,1024,"<tr class=locationlistrow><td>%s</td><td>%s</td></tr>\n",
	       countries[i].iso3166_code,countries[i].country);
      append_string(out,&offset,max_len,piece);
    }
  append_string(out,&offset,max_len,"</table>\n");
  
  out[offset]=0; return 0;
}
