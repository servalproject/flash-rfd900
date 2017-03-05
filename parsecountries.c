/*
  Scrape XML export of https://en.wikipedia.org/wiki/ISO_3166-1 for list
  of all current ISO-3166 codes, including defacto codes.

  The table rows take a similar form to:
  
|-
| [[Afghanistan]]
| [[ISO 3166-1 alpha-2#AF|{{mono|AF}}]] || {{mono|AFG}} || {{mono|004}} || [[ISO 3166-2:AF]]

  We keep the first entry for a code we encounter, since currently valid codes come
  first in the wikipedia page.

*/

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

int count=0;

int process_entry(FILE *of,char *country,char *code)
{
  char clean_country[1024];
  int len=0;
  int incomment=0;

  for(int i=0;country[i];i++) {  
  
    // Remove HTML comments
    if (!strncmp("&lt;",&country[i],4)) incomment=1;
    if (i>4)
      if (!strncmp("&gt;",&country[i-4],4)) incomment=0;

    if (!incomment) {
      // Change | to / to indicate alternate options
      if (country[i]=='\"') country[i]='\'';
      else if (country[i]!='|') clean_country[len++]=country[i];
      else clean_country[len++]='/';
    }    
  }
  clean_country[len]=0;
  fprintf(of,"  {\"%s\",\"%s\"},\n",clean_country,code);
  count++;
  
  return 0;
}

int main(int argc,char **argv)
{
  char buffer[1024*1024];

  if (argc!=3) {
    fprintf(stderr,"usage: parsecountries ISO_3166-1.xml countries.h\n");
    exit(-1);
  }
  
  FILE *f=fopen(argv[1],"r");
  if (!f) {
    perror("Failed to open input file");
    exit(-1);
  }
  
  FILE *of=fopen(argv[2],"w");
  if (!of) {
    perror("Failed to open output file");
    exit(-1);
  }

  fprintf(of,
	  "struct country {\n"
	  "  char *country;\n"
	  "  char *iso3166_code;\n"
	  "};\n"
	  "\n"
	  "struct country countries[]={\n");
  
  int r=fread(buffer,1,1024*1024,f);
  fclose(f);

  char line[1024];
  int len=0;
  int state=0;
  char country[1024];
  char iso_code[1024];

  for(int i=0;i<r;i++) {
    if ((buffer[i]=='\n')||(buffer[i]=='\r')) {
      line[len]=0;
      switch (state) {
      case 0: // Waiting for table row
	if (!strncmp(line,"|-",2)) state=1;
	break;
      case 1: // In table row, looking for country name
	if (sscanf(line,"| [[%[^]]]]",country)==1) state=2; else state=0;
	break;
      case 2: // Got country name, looking for data row
	if (sscanf(line,"| [[ISO 3166-1 alpha-2#%[^|]",iso_code)==1) {
	  state=0;
	  process_entry(of,country,iso_code);
	}
	state=0;
	break;
      } 
      len=0;
    } else {
      if (len<1000) line[len++]=buffer[i];
    }
  }

  fprintf(of,
	  "{NULL,NULL}\n"
	  "};\n");
  fclose(of);
  fprintf(stderr,
	  "Extracted %d countries from https://en.wikipedia.org/wiki/ISO_3166-1\n",
	  count);
  return 0;
  
}
