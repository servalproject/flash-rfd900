#include <stdio.h>
#include <stdlib.h>
#include <sys/fcntl.h>
#include <strings.h>
#include <string.h>
#include <unistd.h>
#include "flash900.h"
#include "sha3.h"

struct radio_parameters {
  char primary_country[2];
  char lock_firmware;
  uint16_t airspeed;
  uint32_t frequency;
  unsigned char txpower;
  unsigned char dutycycle;
};

// Radio parameters we get from the EEPROM
struct radio_parameters eeprom_radio_parameters;
char regulatory_information[16384]="No regulatory information provided.";
unsigned long regulatory_information_length=
  strlen((const char *)"No regulatory information provided.");
char configuration_directives[16384]="nodirectives=true\n";
unsigned long configuration_directives_length=strlen("nodirectives=true\n");

// Use public domain libz-compatible library
#define MINIZ_NO_ARCHIVE_APIS
#define MINIZ_NO_ARCHIVE_WRITING_APIS
//#define MINIZ_NO_ZLIB_APIS
#include "miniz.c"

int eeprom_write_page(int fd, int address,unsigned char *readblock);

struct directive {
  char *key;
  char *value;
};

#define MAX_DIRECTIVES 1024
struct directive directives[MAX_DIRECTIVES];
int directive_count=0;

int directives_to_list()
{
  int i;
  for(i=0;i<directive_count;i++) {
    free(directives[i].key); directives[i].key=NULL;
    free(directives[i].value); directives[i].value=NULL;
  }
  directive_count=0;

  char key[1024];
  char value[1024];
  char line[1024];
  int len=0;

  for(i=0;i<=strlen(configuration_directives);i++) {
    switch(configuration_directives[i]) {
    case '\n': case '\r': case 0:
      line[len]=0;
      if (sscanf(line,"%[^=]=%[^=\r\n]",key,value)==2) {
	if (directive_count<MAX_DIRECTIVES) {
	  directives[directive_count].key=strdup(key);
	  directives[directive_count++].value=strdup(value);
	} else {
	  fprintf(stderr,"Configuration directives block contains too many entries (max = %d)\n",MAX_DIRECTIVES);
	  exit(-1);
	}	      
      }
      len=0;
      break;
    default:
      if (len<1000) line[len++]=configuration_directives[i];
      break;
    }
  }
  return 0;
}

int directives_from_list()
{
  int i;
  char line[1024];
  int offset=0;
  for(i=0;i<directive_count;i++) {
    if ((strlen(directives[i].key)+strlen(directives[i].value))>=1000) {
      fprintf(stderr,"Directive too long: %s=%s\n",
	      directives[i].key,directives[i].value);
      exit(-1);
    }
    snprintf(line,1024,"%s=%s\n",directives[i].key,directives[i].value);
    if ((strlen(line)+offset)<16383) {
      bcopy(line,&configuration_directives[offset],strlen(line));
      offset+=strlen(line);
    } else {
      fprintf(stderr,"Total length of configuration directives too long.\n");
      exit(-1);
    }
  }
  configuration_directives[offset]=0;
  configuration_directives_length=offset;
  return 0;
}

int directive_set_value(char *directive_key,char *directive_value)
{
  int i;
  for(i=0;i<directive_count;i++)
    if (!strcasecmp(directive_key,directives[i].key))
      {
	if (directive_value[0]) {
	  // Set directive value
	  free(directives[i].value);
	  directives[i].value=strdup(directive_value);
	} else {
	  // Clear directive
	  free(directives[i].key); free(directives[i].value);
	  directives[i].key=NULL; directives[i].value=NULL;
	  directives[i]=directives[directive_count-1];
	  directive_count--;	   
	}
	break;
      }
  if (i==directive_count&&directive_value[0]) {
    if (directive_count>=MAX_DIRECTIVES) {
      fprintf(stderr,"Too many configuration directives (limit = %d)\n",
	      MAX_DIRECTIVES);
      exit(-1);
    } else {
      directives[directive_count].key=strdup(directive_key);
      directives[directive_count].value=strdup(directive_value);
      directive_count++;
    }
  }
  return 0;
}

#define READ_ALL 1
#define DIRECTIVES_ONLY 2
int eeprom_decode_data(unsigned char *datablock,int flags)
{

  // Parse radio parameter block
  // See http://developer.servalproject.org/dokuwiki/doku.php?id=content:meshextender:me2.0_eeprom_plan&#memory_layout

  int problems=0;

  if (flags==READ_ALL) {
    // Clear out read data before proceeding
    bzero(&eeprom_radio_parameters,sizeof(eeprom_radio_parameters));
    regulatory_information_length=0;
    regulatory_information[0]=0;
    configuration_directives_length=0;
    configuration_directives[0]=0;
  
    sha3_Init256();
    sha3_Update(&datablock[0x7C0],(0x7F0-0x7C0));
    sha3_Finalize();
    int i;
    for(i=0;i<16;i++) if (datablock[0x7F0+i]!=ctx.s[i>>3][i&7]) break;
    if (i==16) {
      if (!silent_mode)
	fprintf(stderr,"Radio parameter block checksum valid.\n");
      
      uint8_t format_version=datablock[0x7EF];
      eeprom_radio_parameters.primary_country[0]=datablock[0x7ED];
      eeprom_radio_parameters.primary_country[1]=datablock[0x7EE];
      eeprom_radio_parameters.lock_firmware=datablock[0x7E8];
      eeprom_radio_parameters.airspeed=(datablock[0x7E7]<<8)+(datablock[0x7E6]<<0);
      eeprom_radio_parameters.frequency=
	(datablock[0x7E3]<<8)+(datablock[0x7E2]<<0)+
	(datablock[0x7E5]<<24)+(datablock[0x7E4]<<16);
      eeprom_radio_parameters.txpower=datablock[0x7E1];
      eeprom_radio_parameters.dutycycle=datablock[0x7E0];
      
      if (format_version!=0x01) {
	fprintf(stderr,"Radio parameter block data format version is 0x%02x, which I don't understand.\n",format_version);
	problems++;
      }
      
    }
    else {
      fprintf(stderr,
	      "ERROR: Radio parameter block checksum is wrong:\n"	       
	      "       Radio will ignore EEPROM data!\n");
      problems++;
    }
    
    // Parse extended regulatory information (country list etc)
    sha3_Init256();
    sha3_Update(&datablock[0x0400],0x7B0-0x400);
    sha3_Finalize();
    for(i=0;i<16;i++) if (datablock[0x7B0+i]!=ctx.s[i>>3][i&7]) break;
    if (i==16) {
      if (!silent_mode)
	fprintf(stderr,
		"Radio regulatory information text checksum is valid.\n");
      regulatory_information_length=sizeof(regulatory_information);
      int result=mz_uncompress((unsigned char *)regulatory_information,
			       &regulatory_information_length,
			       &datablock[0x400], 0x7B0-0x400);
      if (result!=MZ_OK) {
	fprintf(stderr,"Failed to decompress regulatory information block.\n");
	problems++;
      }
    } else {
      fprintf(stderr,
	      "ERROR: Radio regulatory information text checksum is wrong:\n"	       
	      "       LBARD will report only ISO code from radio parameter block.\n");
      problems++;
    }
  }
  
  // Parse user extended information area
  sha3_Init256();
  sha3_Update(&datablock[0x000],0x3F0);
  sha3_Finalize();
  int i;
  for(i=0;i<16;i++) if (datablock[0x3F0+i]!=ctx.s[i>>3][i&7]) break;
  if (i==16) {
    if (!silent_mode)
      fprintf(stderr,
	      "Mesh-Extender configuration directive text checksum is valid.\n");
    configuration_directives_length=sizeof(configuration_directives);
    int result=mz_uncompress((unsigned char *)configuration_directives,
			     &configuration_directives_length,
			     &datablock[0x0], 0x3F0);
    if (result!=MZ_OK) {
      fprintf(stderr,"Failed to decompress configuration directive block.\n");
      problems++;
    }
  } else {
    fprintf(stderr,
	    "ERROR: Mesh-Extender configuration directive block checksum is wrong:\n");
    problems++;
  }

  if (problems)
    fprintf(stderr,"WARNING: Encountered problems decoding EEPROM data.\n"
	    "         Some data may be incorrect or corrupt.\n");
  
  return problems;
}

int eeprom_display_data(char *msg)
{
  int i;
  
  fprintf(stderr,"%s\n",msg);
  
  fprintf(stderr,
	  "                radio max TX power = %d dBm\n",
	  (int)eeprom_radio_parameters.txpower);
  fprintf(stderr,
	  "              radio max duty-cycle = %d %%\n",
	  (int)eeprom_radio_parameters.dutycycle);
  fprintf(stderr,
	  "                   radio air speed = %d Kbit/sec\n",
	  (int)eeprom_radio_parameters.airspeed);
  fprintf(stderr,
	  "            radio centre frequency = %d Hz\n",
	  (int)eeprom_radio_parameters.frequency);
  fprintf(stderr,
	  "regulations require firmware lock? = '%c'\n",
	  eeprom_radio_parameters.lock_firmware);
  fprintf(stderr,
	  "          primary ISO country code = \"%c%c\"\n",
	  eeprom_radio_parameters.primary_country[0],
	  eeprom_radio_parameters.primary_country[1]
	  );

  fprintf(stderr,
	  "Stored configuration directives are as follows:\n  > ");
  for(i=0;configuration_directives[i];i++) {
    if (configuration_directives[i])
      fprintf(stderr,"%c",configuration_directives[i]);
    if ((configuration_directives[i]=='\r')||(configuration_directives[i]=='\n'))
      fprintf(stderr,"  > ");
  }
  fprintf(stderr,"\n");
  
  
  fprintf(stderr,
	  "Stored regulatory information text is as follows:\n  > ");
  for(i=0;regulatory_information[i];i++) {
    if (regulatory_information[i]) fprintf(stderr,"%c",regulatory_information[i]);
    if ((regulatory_information[i]=='\r')||(regulatory_information[i]=='\n'))
      fprintf(stderr,"  > ");
  }
  fprintf(stderr,"\n");

  
  return 0;
}


int eeprom_parse_line(char *line,unsigned char *datablock)
{
  int address;
  int b[16];
  int err;
  if (sscanf(line,"EPR:%x : READ ERROR #%d",&address,&err)==2)
    {
      fprintf(stderr,"EEPROM read error #%d @ 0x%x\n",err,address);
      for(int i=0;i<16;i++) datablock[address+i]=0xee;
    }

  if (sscanf(line,"EPR:%x : %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x",
	     &address,
	     &b[0],&b[1],&b[2],&b[3],
	     &b[4],&b[5],&b[6],&b[7],
	     &b[8],&b[9],&b[10],&b[11],
	     &b[12],&b[13],&b[14],&b[15])==17) {
    for(int i=0;i<16;i++) datablock[address+i]=b[i];
  }
  
  return 0;
}

char line[1024];
int line_len=0;

long long gettime_ms();

int eeprom_parse_output(int fd,unsigned char *datablock, int address)
{
  char buffer[16384];
  int offset=0;
  int count;

  int bytes=160+15;
  if (address<0x10) bytes-=2;
  else if (address<0x100) bytes-=1;

  long long start=gettime_ms();
  long long now;
  while(1) {
    count=get_radio_reply(fd,&buffer[offset],16384-offset,0);
    now=gettime_ms();
    // if (count>0) fprintf(stderr,"* %d bytes at T+%lldms\n",count,now-start);
    if (count>0) offset+=count;
    if (offset>=bytes) break;
    usleep(1000);
    if ((now-start)>400) break;
  }
  count=offset;
  // now=gettime_ms();
  // if (count>0) fprintf(stderr,"%d bytes at T+%lldms\n",count,now-start);
    
  
  for(int i=0;i<count;i++) {
    if (line_len) {
      if (buffer[i]!='\r')
	{ if (line_len<1000) line[line_len++]=buffer[i]; }
      else {
	line[line_len]=0;
	eeprom_parse_line(line,datablock);
	line_len=0;
      }
    } else {
      if ((buffer[i]=='E')&&(buffer[i+1]=='P')) {
	line[0]='E'; line_len=1;
      }
      if ((buffer[i]==5)&&(buffer[i+1]==16)) {
	// Parse binary format right out
	int address=(unsigned char)buffer[i+2]+((unsigned char)buffer[i+3]<<8);
	if (address>=0&&address<0x800) {
	  for(int j=0;j<16;j++) datablock[address+j]=(unsigned char)buffer[i+4+j];
	  if (0) {
	    fprintf(stderr,
		    "Parsed binary line for addr 0x%03X (from %02X %02X %02X %02x) @ 0x%x\n    ",
		    address,
		    (unsigned char)buffer[i+0],
		    (unsigned char)buffer[i+1],
		    (unsigned char)buffer[i+2],
		    (unsigned char)buffer[i+3],i
		    );
	    for(int j=0;j<16;j++) fprintf(stderr," %02X",datablock[address+j]);
	    fprintf(stderr,"\n");
	  }
	  i+=2+2+16-1;
	}
      }
    }
  }
  if (line_len) eeprom_parse_line(line,datablock);
  
  return 0;
}

int read_eeprom_block(int fd,unsigned char *readblock,int address)
{
  char cmd[1024];
  snprintf(cmd,1024,"!C");
  write_radio(fd,(unsigned char *)cmd,strlen(cmd));
  usleep(1000);
  snprintf(cmd,1024,"%x!g",address);
  write_radio(fd,(unsigned char *)cmd,strlen(cmd));
  usleep(5000);
  snprintf(cmd,1024,"!I");
  write_radio(fd,(unsigned char *)cmd,strlen(cmd));
  // long long start=gettime_ms();
  eeprom_parse_output(fd,readblock,address);
  // long long end=gettime_ms();
  // fprintf(stderr,"[%lldms]",end-start);
  return 0;
}

int read_eeprom_directives(int fd,unsigned char *readblock)
{
  int address;
  if (!silent_mode) fprintf(stderr,"Reading directives data from EEPROM"); fflush(stderr);
  for(address=0;address<0x400;address+=0x80) {
    read_eeprom_block(fd,readblock,address);
    if (!(readblock[address+0x7D]
	  |readblock[address+0x7E]
	  |readblock[address+0x7F]))
      break;
    if (!silent_mode) fprintf(stderr,"."); fflush(stderr);
  }
  // Make sure we read hash
  if (address<0x400) {
    read_eeprom_block(fd,readblock,0x3F0);    
  }
  if (!silent_mode) fprintf(stderr,"\n"); fflush(stderr);
  return 0;
}


int read_entire_eeprom(int fd,unsigned char *readblock)
{
  int address;
  if (!silent_mode) fprintf(stderr,"Reading data from EEPROM"); fflush(stderr);
  for(address=0;address<0x800;address+=0x80) {
    read_eeprom_block(fd,readblock,address);
    if (!silent_mode) fprintf(stderr,"."); fflush(stderr);
  }
  if (!silent_mode) fprintf(stderr,"\n"); fflush(stderr);
  return 0;
}

int write_entire_eeprom(int fd,unsigned char *datablock,unsigned char *readblock)
{
  // Use <addr>!g!y<data>!w sequence to write each 16 bytes
  int problems=0,address;
  if (!silent_mode) fprintf(stderr,"Writing data to EEPROM\n"); fflush(stderr);
  
  for(address=0;address<0x800;address+=0x10) {
    
    // Only write if it has changed
    int changed=0;
    if (readblock) {
      for(int j=0;j<0x10;j++)
	if (datablock[address+j]!=readblock[address+j])
	  { changed=1; break; }
    } else changed=1;
    
    if (!changed) continue;
    
    // Try several times to write
    int result=eeprom_write_page(fd,address,datablock);
    if (result) {
      int retries=10;
      while(retries--) {
	result=eeprom_write_page(fd,address,datablock);
	if (!result) break;
      }
    }
    
    problems+=result;
        
    if (!silent_mode)
      fprintf(stderr,"\rWrote $%x - $%x",address,address+0x10-1); fflush(stderr);
  }
  if (!silent_mode) fprintf(stderr,"\n");
  if (problems)
    fprintf(stderr,
	    "WARNING: A total of %d problems occurred during writing.\n",problems);
  return problems;
}

int eeprom_build_image(char *configuration_directives_normalised,
		       char *regulatory_information,
		       struct radio_parameters radio_parameters,
		       unsigned char *datablock)
{
  unsigned long bytes_used=0x3F0;
  int result=mz_compress2(&datablock[0x000],&bytes_used,
			  (unsigned char *)configuration_directives_normalised,
			  strlen(configuration_directives_normalised)+1,9);
  if (result!=MZ_OK) {
    fprintf(stderr,"Failed to compress configuration directives (MZ result=%d.\n",
	    result);
    return(-1);
  }
  
  bytes_used=0x7B0-0x400;
  result=mz_compress2(&datablock[0x400],&bytes_used,
		      (unsigned char *)regulatory_information,
		      strlen(regulatory_information)+1,9);
  if (result!=MZ_OK) {
    fprintf(stderr,"Failed to compress regulatory information (MZ result=%d.\n",
	    result);
    return(-1);
  } else
    if (!silent_mode)
      fprintf(stderr,"Regulatory information text required %d bytes (0x%03x-0x%03x)\n",
	      (int)bytes_used,0x400,0x400+(int)bytes_used);

  // Set format version
  datablock[0x7EF]=0x01;
  // Set other radio fields for use by RFD900 radio (and also for LBARD display)
  datablock[0x7ED]=radio_parameters.primary_country[0];
  datablock[0x7EE]=radio_parameters.primary_country[1];
  datablock[0x7E8]=radio_parameters.lock_firmware;
  datablock[0x7E6]=radio_parameters.airspeed&0xff;
  datablock[0x7E7]=radio_parameters.airspeed>>8;
  datablock[0x7E2]=(radio_parameters.frequency>>0)&0xff;
  datablock[0x7E3]=(radio_parameters.frequency>>8)&0xff;
  datablock[0x7E4]=(radio_parameters.frequency>>16)&0xff;
  datablock[0x7E5]=(radio_parameters.frequency>>24)&0xff;
  datablock[0x7E1]=radio_parameters.txpower;
  datablock[0x7E0]=radio_parameters.dutycycle;
  
  // Write hashes
  int i;
  
  // Mesh Extender configuration directive block
  sha3_Init256();
  sha3_Update(&datablock[0],0x3F0);
  sha3_Finalize();
  for(i=0;i<16;i++) datablock[0x3F0+i]=ctx.s[i>>3][i&7];
  
  // Regulatory info block
  sha3_Init256();
  sha3_Update(&datablock[0x400],0x7B0-0x400);
  sha3_Finalize();
  for(i=0;i<16;i++) datablock[0x7B0+i]=ctx.s[i>>3][i&7];
  
  // Radio parameter block
  sha3_Init256();
  sha3_Update(&datablock[0x7C0],0x7F0-0x7C0);
  sha3_Finalize();
  for(i=0;i<16;i++) datablock[0x7F0+i]=ctx.s[i>>3][i&7];

  return 0;
}

void usage()
{
  fprintf(stderr,"Version 20170421.1557.1\n");
  fprintf(stderr,"usage: flash900 <firmware> <serial port> [force|verify|230400|115200|57600]\n");

  fprintf(stderr,"usage: flash900 eeprom <serial port> [<Mesh Extender configuration directives|\"\"> <alternate regulatory information|\"\"> <frequency> <txpower> <dutycycle> <airspeed> <primary country 2-letter code> <firmware lock (Y|N)> <full list of ISO 2-letter country codes>]\n");
  fprintf(stderr,"       flash900 eeprom <serial port> directives\n");
  fprintf(stderr,"       flash900 eeprom <serial port> directives list>\n");
  fprintf(stderr,"       flash900 eeprom <serial port> directives clear>\n");
  fprintf(stderr,"       flash900 eeprom <serial port> directives get <key>\n");
  fprintf(stderr,"       flash900 eeprom <serial port> directives del <key>\n");
  fprintf(stderr,"       flash900 eeprom <serial port> directives set <key> <value>\n");
  fprintf(stderr," e.g.: flash900 eeprom /dev/cu.usbserial-AARDVARK \"OTABID=918f8a6684c861f68c1f6c468c4c684\\nMESHEXTENDERNAME=Adelaide1\\nLATITUDE=-35\\nLONGITUDE=+138\\n\" \"\" 923000000 24 100 128 AU N AU,NZ,US,CA,VU\n");
  fprintf(stderr," e.g.: flash900 eeprom /dev/cu.usbserial-AARDVARK\n");
  fprintf(stderr," e.g.: flash900 eeprom /dev/cu.usbserial-AARDVARK directive get OTABID\n");
  fprintf(stderr," e.g.: flash900 eeprom /dev/cu.usbserial-AARDVARK directive set MESHEXTENDERNAME \"my mesh extender\"\n");
  return;
}

int silent_mode=0;

int eeprom_program(int argc,char **argv)
{
  if ((argc!=12)&&((argc<3)||(argc>7))) {
    usage(); exit(-1);
  }
  
  int parameters_set=0;
  int parameters_show=0;
  int directive_get=0;
  int directive_set=0;
  int directive_list=0;
  int directive_clear=0;
  char *directive_key=NULL;
  char *directive_value=NULL;
  int dump=0;
  
  if ((argc>3)&&(argc<8)) {
    if (!strcasecmp(argv[3],"dump")) {
      dump=1;
    }
    else if (strcasecmp(argv[3],"directives")) { usage(); exit(-1); }
    silent_mode=1;
    if (argc>4) {
      if (!strcasecmp(argv[4],"clear")) {
	if (argc==5) directive_clear=1;
      } else if (!strcasecmp(argv[4],"list")) {
	if (argc==5) directive_list=1;
      } else if (!strcasecmp(argv[4],"get")) {
	if (argc==6) {
	  directive_get=1;
	  directive_key=argv[5];
	} else { usage(); exit(-1); }
      } else if (!strcasecmp(argv[4],"del")) {
	if (argc==6) {
	  directive_key=argv[5];
	  directive_value="";
	  directive_set=1;
	}	
      } else if (!strcasecmp(argv[4],"set")) {
	if (argc==7) {
	  directive_key=argv[5];
	  directive_value=argv[6];
	  directive_set=1;
	} else { usage(); exit(-1); }
      } else  { usage(); exit(-1); }
    }
  }
  if (argc==12) parameters_set=1;
  if (argc==3) parameters_show=1;

  struct radio_parameters radio_parameters;
  
  char *configuration_directives_input=argv[3];
  char *regulatory_information_input=argv[4];
  char *country_list=argv[11];

  // Start with blank memory block
  unsigned char datablock[2048];
  memset(&datablock[0],0,2048-64);
  memset(&datablock[2048-64],0,64);

  int fd=open(argv[2],O_RDWR);
  if (fd==-1) {
    fprintf(stderr,"Could not open serial port '%s'\n",argv[2]);
    exit(-1);
  }
  if (set_nonblock(fd)) {
    fprintf(stderr,"Could not set serial port '%s' non-blocking\n",argv[2]);
    exit(-1);
  }

  // XXX - Short-circuit detection with ! command test to save time
  {
    setup_serial_port(fd,230400);
    char buffer[1024];
    clear_waiting_bytes(fd);
    write_radio(fd,(unsigned char *)"0!g",3);
    usleep(20000);
    write_radio(fd,(unsigned char *)"0!g",3);
    usleep(20000);
    int count=get_radio_reply(fd,buffer,1024,0);
    if (memmem(buffer,count,"EPRADDR=$0",10)) {
      if (!silent_mode) fprintf(stderr,"Radio is ready.\n");
    } else {
      if (detect_speed(fd)) {
	fprintf(stderr,"Could not detect radio speed and mode. Sorry.\n");
	exit(-1);
      }
      if (atmode)
	if (switch_to_online_mode(fd)) {
	  fprintf(stderr,"Could not switch to online mode.\n");
	  exit(-1);
	}      
    }
  }

  unsigned char readblock[2048];

  if (parameters_set) {
    // Compress user data and alternate regulatory information into EEPROM.
    // If no alternate regulatory information is provided, generate default
    // information based on the set of countries listed.

    char configuration_directives_normalised[16384];
    int cdn_len=0;
    for(int i=0;configuration_directives_input[i];i++) {
      if (configuration_directives_input[i]=='\\') {
	i++;
	switch(configuration_directives_input[i]) {
	case 'n': configuration_directives_normalised[cdn_len++]='\n'; break;
	case 'r': configuration_directives_normalised[cdn_len++]='\r'; break;
	case 'b': configuration_directives_normalised[cdn_len++]='\b'; break;
	case '\\': configuration_directives_normalised[cdn_len++]='\\'; break;
	}
      } else configuration_directives_normalised[cdn_len++]
	       =configuration_directives_input[i];
    }

    // Set actual radio parameters
    radio_parameters.frequency=atoi(argv[5]);
    radio_parameters.txpower=atoi(argv[6]?argv[6]:"0");  
    radio_parameters.dutycycle=atoi(argv[7]?argv[7]:"0");  
    radio_parameters.airspeed=atoi(argv[8]?argv[8]:"0");
    radio_parameters.primary_country[0]=argv[9][0];
    radio_parameters.primary_country[1]=argv[9][1];
    radio_parameters.lock_firmware=argv[10][0];

    // Generate default regulatory information, if required
    if (!regulatory_information_input[0]) {
      generate_regulatory_information(regulatory_information,
				      16384,
				      radio_parameters.primary_country,
				      country_list,
				      radio_parameters.frequency,
				      radio_parameters.txpower,
				      radio_parameters.dutycycle);
      fprintf(stderr,"Auto-generating regulatory boilerplate information text"
	      " (%d bytes long).\n",(int)strlen(regulatory_information));
    } else {
      strncpy(regulatory_information,regulatory_information_input,16384);
      fprintf(stderr,"Using user-supplied regulatory information text.\n");
    }

    if (!configuration_directives_input[0]) {
      // Keep old configuration directives
      read_eeprom_directives(fd,readblock);
      eeprom_decode_data(readblock,DIRECTIVES_ONLY);
      strcpy(configuration_directives_normalised,
	     configuration_directives);
    }
    
    if (eeprom_build_image(configuration_directives_normalised,
			   regulatory_information,
			   radio_parameters,
			   datablock)) {
      fprintf(stderr,"Could not build datablock to write to EEPROM.\n");
      exit(-1);
    }
  }

  if (dump) {
    read_entire_eeprom(fd,readblock);
    dump_bytes("EEPROM contents",readblock,2048);
    return 0;
  }
  
  if (directive_list) {
    read_eeprom_directives(fd,readblock);
    eeprom_decode_data(readblock,DIRECTIVES_ONLY);
    directives_to_list();
    for(int i=0;i<directive_count;i++)
      printf("%s=%s\n",directives[i].key,directives[i].value);
    return 0;
  }
  
  if (directive_get) {
    read_eeprom_directives(fd,readblock);
    eeprom_decode_data(readblock,DIRECTIVES_ONLY);
    directives_to_list();
    for(int i=0;i<directive_count;i++)
      if (!strcasecmp(directive_key,directives[i].key))
	printf("%s=%s\n",directives[i].key,directives[i].value);
    return 0;
  }

  if (directive_clear) {
    read_entire_eeprom(fd,readblock);
    eeprom_decode_data(readblock,READ_ALL);
    configuration_directives[0]=0;
    if (eeprom_build_image(configuration_directives,
			   regulatory_information,
			   eeprom_radio_parameters,
			   datablock)) {
      fprintf(stderr,"Could not build datablock to write to EEPROM.\n");
      exit(-1);
    }
    write_entire_eeprom(fd,datablock,readblock);    
  }

  if (directive_set) {
    read_entire_eeprom(fd,readblock);
    eeprom_decode_data(readblock,READ_ALL);
    directives_to_list();
    directive_set_value(directive_key,directive_value);
    directives_from_list();
    if (eeprom_build_image(configuration_directives,
			   regulatory_information,
			   eeprom_radio_parameters,
			   datablock)) {
      fprintf(stderr,"Could not build datablock to write to EEPROM.\n");
      exit(-1);
    }
    write_entire_eeprom(fd,datablock,readblock);    
  }
  
  
  if (parameters_set) {

    // Read current contents
    read_entire_eeprom(fd,readblock);

    // Write new contents, using read data to suppress unnecessary writes
    write_entire_eeprom(fd,datablock,readblock);
  }
  
  // Verify it
  unsigned char verifyblock[2048];
  read_entire_eeprom(fd,verifyblock);
  int address;
  int problems=0;
  if (parameters_set) {
    for(address=0;address<0x800;address++) {
      if (verifyblock[address]!=datablock[address]) problems++;
    }
  }
  
  if (problems) {
    fprintf(stderr,
	    "ERROR: %d bytes could not be correctly written.\n"
	    "       EEPROM data is now most likely corrupt.\n",problems);
    eeprom_decode_data(verifyblock,READ_ALL);
    eeprom_display_data("Datablock read from EEPROM");
    fprintf(stderr,
	    "ERROR: %d bytes could not be correctly written.\n"
	    "       EEPROM data is now most likely corrupt.\n",problems);
    return -1;
  }
  if (parameters_show) {
    eeprom_decode_data(verifyblock,READ_ALL);
    eeprom_display_data("Datablock read from EEPROM");
  }

  
    
  return 0;      
}

int eeprom_write_page(int fd, int address,unsigned char *datablock)
{
  int problems=0;
  char cmd[1024];
  unsigned char reply[8193];
  int r;

  int address_not_yet_set=5;
  while(address_not_yet_set)
    {
      snprintf(cmd,1024,"!C%x!g",address);
      write_radio(fd,(unsigned char *)cmd,strlen(cmd));
      usleep(15000);
      
      int a,o;
      r=read(fd,reply,8192); reply[8192]=0;
      // debug++; dump_bytes(cmd,reply,r); debug--;
      // Skip any other stuff
      for(o=0;o<r;o++) if (!strncmp("EPRADDR=",(const char *)&reply[o],8)) {
	  // fprintf(stderr,"EPRADDR= found @ offset 0x%x\n",o);
	  break;
	}
      if (sscanf((const char *)&reply[o],"EPRADDR=$%x",&a)!=1) {
	// fprintf(stderr,"WARNING: Could not set EEPROM write address @ 0x%x\n",address);
      } else if (a!=address) {
	// fprintf(stderr,"WARNING: EEPROM write address set wrong @ 0x%x (got set to 0x%x, command was '%s')\n",address,a,cmd);
      } else { address_not_yet_set=0; break; }
      address_not_yet_set--;
      if (!address_not_yet_set) {
	fprintf(stderr,"ERROR: Could not set EEPROM write address after 5 attempts.\n");
	problems++;
      }
    }
  
  // Now write data bytes, escaping ! as !.
  for(int j=0;j<0x10;j++) {
    if (datablock[address+j]=='!') write_radio(fd,(unsigned char *)"!.",2);
    else write_radio(fd,&datablock[address+j],1);
    // write_radio(fd,(unsigned char *)&"ABCDEFGHIJKLMNOP"[j],1);
  }
  usleep(1000);
  write_radio(fd,(unsigned char *)"!y",2);
  usleep(1000);
  write_radio(fd,(unsigned char *)"!w",2);
  usleep(71000);

  // Allow more time since we have slowed down I2C
  usleep(100000);
  
  //  snprintf(cmd,1024,"%x!g!E",address);
  //  write_radio(fd,(unsigned char *)cmd,strlen(cmd));
  //  usleep(50000);
  // Check for "EEPROM WRITTEN $%x -> $%x" or "WRITE ERROR" messages
  
  // clear out any queued data first
  r=read(fd,reply,8192); reply[8192]=0;
  char expected[1024];
  snprintf(expected,1024,"%X %X %X %X %X %X %X %X %X %X %X %X %X %X %X %X\r\r\nEEPROM WRITTEN @ $%X\r\r\nREAD BACK %X %X %X %X %X %X %X %X %X %X %X %X %X %X %X %X\r",
	   datablock[address+0],datablock[address+1],
	   datablock[address+2],datablock[address+3],
	   datablock[address+4],datablock[address+5],
	   datablock[address+6],datablock[address+7],
	   datablock[address+8],datablock[address+9],
	   datablock[address+10],datablock[address+11],
	   datablock[address+12],datablock[address+13],
	   datablock[address+14],datablock[address+15],
	   address,
	   datablock[address+0],datablock[address+1],
	   datablock[address+2],datablock[address+3],
	   datablock[address+4],datablock[address+5],
	   datablock[address+6],datablock[address+7],
	   datablock[address+8],datablock[address+9],
	   datablock[address+10],datablock[address+11],
	   datablock[address+12],datablock[address+13],
	   datablock[address+14],datablock[address+15]
	   );
  if (strstr((char *)reply,"WRITE ERROR")) {
    fprintf(stderr,"\nERROR: Write error writing to EEPROM @ 0x%x\n",address);
    problems++;
  }
  if (!strstr((char *)reply,expected)) {
    fprintf(stderr,"\nERROR: No write confirmation received from EEPROM @ 0x%x\n",address);
    debug++; dump_bytes("This is what I saw",reply,r); debug--;
    debug++; dump_bytes("I expected to see",
			(unsigned char *)expected,strlen(expected)); debug--;
    
    problems++;
  }

  return problems;
}
