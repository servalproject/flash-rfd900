#include <stdio.h>
#include <stdlib.h>
#include <sys/fcntl.h>
#include <strings.h>
#include <string.h>
#include <unistd.h>
#include "flash900.h"
#include "sha3.h"

// Radio parameters we get from the EEPROM
char regulatory_information[16384]="No regulatory information provided.";
unsigned long regulatory_information_length=
  strlen("No regulatory information provided.");
char configuration_directives[16384]="nodirectives=true\n";
unsigned long configuration_directives_length=strlen("nodirectives=true\n");

// Use public domain libz-compatible library
#define MINIZ_NO_ARCHIVE_APIS
#define MINIZ_NO_ARCHIVE_WRITING_APIS
//#define MINIZ_NO_ZLIB_APIS
#include "miniz.c"

int eeprom_write_page(int fd, int address,unsigned char *readblock);

int eeprom_decode_data(char *msg,unsigned char *datablock)
{

  // Parse radio parameter block
  // See http://developer.servalproject.org/dokuwiki/doku.php?id=content:meshextender:me2.0_eeprom_plan&#memory_layout

  sha3_Init256();
  sha3_Update(&datablock[0x7C0],(0x7F0-0x7C0));
  sha3_Finalize();
  int i;
  for(i=0;i<16;i++) if (datablock[0x7F0+i]!=ctx.s[i>>3][i&7]) break;
  if (i==16) {
    fprintf(stderr,"Radio parameter block checksum valid.\n");

    uint8_t format_version=datablock[0x7EF];
    char primary_iso_code[3]={datablock[0x7ED],datablock[0x7EE],0};
    uint8_t regulatory_lock_required=datablock[0x7E8];
    uint16_t radio_bitrate=(datablock[0x7E7]<<8)+(datablock[0x7E6]<<0);
    uint32_t radio_centre_frequency=
      (datablock[0x7E3]<<8)+(datablock[0x7E2]<<0)+
      (datablock[0x7E5]<<24)+(datablock[0x7E4]<<16);
    uint8_t radio_txpower_dbm=datablock[0x7E1];
    uint8_t radio_max_dutycycle=datablock[0x7E0];

    if (format_version!=0x01) {
      fprintf(stderr,"Radio parameter block data format version is 0x%02x, which I don't understand.\n",format_version);
    } else {    
      fprintf(stderr,
	      "                radio max TX power = %d dBm\n",
	      (int)radio_txpower_dbm);
      fprintf(stderr,
	      "              radio max duty-cycle = %d %%\n",
	      (int)radio_max_dutycycle);
      fprintf(stderr,
	      "                   radio air speed = %d Kbit/sec\n",
	      (int)radio_bitrate);
      fprintf(stderr,
	      "            radio centre frequency = %d Hz\n",
	      (int)radio_centre_frequency);
      fprintf(stderr,
	      "regulations require firmware lock? = '%c'\n",
	      regulatory_lock_required);
      fprintf(stderr,
	      "          primary ISO country code = \"%s\"\n",
	      primary_iso_code);

      // XXX - Store all parameters for reference.  In particular,
      // we care about radio_max_dutycycle and regulatory_lock_required, so that
      // we can obey them.
    }
  }
  else fprintf(stderr,
	       "ERROR: Radio parameter block checksum is wrong:\n"	       
	       "       Radio will ignore EEPROM data!\n");

  // Parse extended regulatory information (country list etc)
  sha3_Init256();
  sha3_Update(&datablock[0x0400],0x7B0-0x400);
  sha3_Finalize();
  for(i=0;i<16;i++) if (datablock[0x7B0+i]!=ctx.s[i>>3][i&7]) break;
  if (i==16) {
    fprintf(stderr,
	    "Radio regulatory information text checksum is valid.\n");
    regulatory_information_length=sizeof(regulatory_information);
    int result=mz_uncompress((unsigned char *)regulatory_information,
			     &regulatory_information_length,
			     &datablock[0x400], 0x7B0-0x400);
    if (result!=MZ_OK)
      fprintf(stderr,"Failed to decompress regulatory information block.\n");
    else {
      // XXX - This should be recorded somewhere, so that we can present it using
      // our web server.
      fprintf(stderr,
	      "The information text is as follows:\n  > ");
      for(i=0;regulatory_information[i];i++) {
	if (regulatory_information[i]) fprintf(stderr,"%c",regulatory_information[i]);
	if ((regulatory_information[i]=='\r')||(regulatory_information[i]=='\n'))
	  fprintf(stderr,"  > ");
      }
      fprintf(stderr,"\n");
    }
  } else fprintf(stderr,
		 "ERROR: Radio regulatory information text checksum is wrong:\n"	       
		 "       LBARD will report only ISO code from radio parameter block.\n");

  // Parse user extended information area
  sha3_Init256();
  sha3_Update(&datablock[0x000],0x3F0);
  sha3_Finalize();
  for(i=0;i<16;i++) if (datablock[0x3E0+i]!=ctx.s[i>>3][i&7]) break;
  if (i==16) {
    fprintf(stderr,
	    "Mesh-Extender configuration directive text checksum is valid.\n"
	    "The information text is as follows:\n  > ");
    configuration_directives_length=sizeof(configuration_directives);
    int result=mz_uncompress((unsigned char *)configuration_directives,
			     &configuration_directives_length,
			     &datablock[0x0], 0x3F0);
    if (result!=MZ_OK)
      fprintf(stderr,"Failed to decompress configuration directive block.\n");
    else {
    for(i=0;configuration_directives[i];i++) {
      if (configuration_directives[i])
	fprintf(stderr,"%c",configuration_directives[i]);
      if ((configuration_directives[i]=='\r')||(configuration_directives[i]=='\n'))
	fprintf(stderr,"  > ");
    }
    fprintf(stderr,"\n");
    }
  } else
    fprintf(stderr,
	    "ERROR: Mesh-Extender configuration directive block checksum is wrong:\n");
  
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

int eeprom_parse_output(int fd,unsigned char *datablock)
{
  char buffer[16384];
  int count=get_radio_reply(fd,buffer,16384,0);
  
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
    }
  }
  if (line_len) eeprom_parse_line(line,datablock);
  
  return 0;
}

int eeprom_program(int argc,char **argv)
{
  if ((argc!=10)&&(argc!=3)) {
    fprintf(stderr,"usage: flash900 eeprom <serial port> [<user data file|-> <protected data file|-> <frequency> <txpower> <airspeed> <primary country 2-letter code> <firmware lock (Y|N)]\n");
    exit(-1);
  }
  
  char *userdatafile=argv[3];
  char *protecteddatafile=argv[4];
  int frequency=atoi(argv[5]);
  int txpower=atoi(argv[6]?argv[6]:"0");
  int airspeed=atoi(argv[7]?argv[7]:"0");
  char *primary_country=argv[8];
  char *lock_firmware=argv[9];

  // Start with blank memory block
  unsigned char datablock[2048];
  memset(&datablock[0],0,2048-64);
  memset(&datablock[2048-64],0,64);

  FILE *f;

  if (argc==10) {
    // Read data files and assemble 2KB data block to write
    if (strcmp(userdatafile,"-")) {
      f=fopen(userdatafile,"r");
      if (!f) {
	perror("Could not open user data file");
      } else {
	fread(&datablock[0],1024,1,f);
	fclose(f);
      }
    }
    if (strcmp(protecteddatafile,"-")) {
      f=fopen(protecteddatafile,"r");
      if (!f) {
	perror("Could not open user data file");
      } else {
	fread(&datablock[1024],1024-64,1,f);
	fclose(f);
      }
    }
    datablock[2048-32+0]=(txpower>>8);
    datablock[2048-32+1]=(txpower&0xff);
    datablock[2048-32+2]=(airspeed>>8);
    datablock[2048-32+3]=(airspeed&0xff);
    datablock[2048-32+4]=(frequency>>8);
    datablock[2048-32+5]=(frequency&0xff);
    datablock[2048-32+13]=lock_firmware[0];
    datablock[2048-32+14]=primary_country[0];
    datablock[2048-32+15]=primary_country[1];

    // Write hashes
    int i;
    sha3_Init256();
    sha3_Update(&datablock[0],1024-16);
    sha3_Finalize();
    for(i=0;i<16;i++) datablock[1024-16+i]=ctx.s[i>>3][i&7];
  
    sha3_Init256();
    sha3_Update(&datablock[2048-64],48);
    sha3_Finalize();
    for(i=0;i<16;i++) datablock[2048-16+i]=ctx.s[i>>3][i&7];
  
    sha3_Init256();
    sha3_Update(&datablock[1024],1024-64-16);
    sha3_Finalize();
    for(i=0;i<16;i++) datablock[2048-64-16+i]=ctx.s[i>>3][i&7];

    
  }
  
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
      fprintf(stderr,"Radio is ready.\n");
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

  char cmd[1024];
  int address;
  fprintf(stderr,"Reading data from EEPROM"); fflush(stderr);
  for(address=0;address<0x800;address+=0x80) {
    snprintf(cmd,1024,"!C");
    write_radio(fd,(unsigned char *)cmd,strlen(cmd));
    usleep(1000);
    snprintf(cmd,1024,"%x!g",address);
    write_radio(fd,(unsigned char *)cmd,strlen(cmd));
    usleep(10000);
    snprintf(cmd,1024,"!E");
    write_radio(fd,(unsigned char *)cmd,strlen(cmd));
    usleep(200000);
    eeprom_parse_output(fd,readblock);
    fprintf(stderr,"."); fflush(stderr);
  }
  fprintf(stderr,"\n"); fflush(stderr);

  
  if (argc==10) {
    // Write it

    eeprom_decode_data("Datablock for writing",datablock);
    
    // Use <addr>!g!y<data>!w sequence to write each 16 bytes
    int problems=0;
    fprintf(stderr,"Writing data to EEPROM\n"); fflush(stderr);

    for(address=0;address<0x800;address+=0x10) {

      // Only write if it has changed
      int changed=0;
      for(int j=0;j<0x10;j++)
	if (datablock[address+j]!=readblock[address+j])
	  { changed=1; break; }
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
      
      
      fprintf(stderr,"\rWrote $%x - $%x",address,address+0x10-1); fflush(stderr);
    }
    fprintf(stderr,"\n");
    if (problems)
      fprintf(stderr,
	      "WARNING: A total of %d problems occurred during writing.\n",problems);
  }
  
  // Verify it
  // Use <addr>!g, !E commands to read out EEPROM data
  unsigned char verifyblock[2048];

  fprintf(stderr,"Reading data from EEPROM"); fflush(stderr);
  for(address=0;address<0x800;address+=0x80) {
    snprintf(cmd,1024,"%x!g",address);
    write_radio(fd,(unsigned char *)cmd,strlen(cmd));
    usleep(20000);
    snprintf(cmd,1024,"!E");
    write_radio(fd,(unsigned char *)cmd,strlen(cmd));
    usleep(300000);
    eeprom_parse_output(fd,verifyblock);
    fprintf(stderr,"."); fflush(stderr);
  }
  fprintf(stderr,"\n"); fflush(stderr);
    
  eeprom_decode_data("Datablock read from EEPROM",verifyblock);
  
  
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
