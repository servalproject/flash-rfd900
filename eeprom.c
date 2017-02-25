#include <stdio.h>
#include <stdlib.h>
#include <sys/fcntl.h>
#include <strings.h>
#include <string.h>
#include <unistd.h>
#include "flash900.h"
#include "sha3.h"

int eeprom_decode_data(char *msg,unsigned char *datablock)
{
  debug++; dump_bytes("Complete EEPROM data",datablock,2048); debug--;

  // Parse radio parameter block
  sha3_Init256();
  sha3_Update(&datablock[2048-64],48);
  sha3_Finalize();
  int i;
  for(i=0;i<16;i++)
    if (datablock[2048-16+i]!=ctx.s[i>>3][i&7]) break;
  if (i==16) {
    fprintf(stderr,"Radio parameter block checksum valid.\n");
    fprintf(stderr,"       TX power = %d dBm\n",
	    (datablock[2048-32+0]<<8)+(datablock[2048-32+1]<<0));
    fprintf(stderr,"      Air speed = %d Kbit/sec\n",
	    (datablock[2048-32+2]<<8)+(datablock[2048-32+3]<<0));
    fprintf(stderr,"      Frequency = %d MHz\n",
	    (datablock[2048-32+4]<<8)+(datablock[2048-32+5]<<0));
    fprintf(stderr,"  Firmware lock = %c\n",
	    datablock[2048-32+13]);
    fprintf(stderr,"       ISO code = %c%c\n",
	    datablock[2048-32+14],datablock[2048-32+15]);
  }
  else fprintf(stderr,
	       "ERROR: Radio parameter block checksum is wrong:\n"	       
	       "       Radio will ignore EEPROM data!\n");

  // Parse extended regulatory information (country list etc)
  sha3_Init256();
  sha3_Update(&datablock[1024],1024-64-16);
  sha3_Finalize();
  for(i=0;i<16;i++)
    if (datablock[2048-64-16+i]!=ctx.s[i>>3][i&7]) break;
  if (i==16) {
    fprintf(stderr,
	    "Radio regulatory information text checksum is valid.\n"
	    "The information text is as follows:\n  > ");
    for(i=1024;i<2048-64-16;i++) {
      if (datablock[i]) fprintf(stderr,"%c",datablock[i]);
      if ((datablock[i]=='\r')||(datablock[i]=='\n')) fprintf(stderr,"  > ");
    }
    fprintf(stderr,"\n");
  } else fprintf(stderr,
		 "ERROR: Radio regulatory information text checksum is wrong:\n"	       
		 "       LBARD will report only ISO code from radio parameter block.\n");

  // Parse user extended information area
  sha3_Init256();
  sha3_Update(&datablock[0],1024-16);
  sha3_Finalize();
  for(i=0;i<16;i++)
    if (datablock[1024-16+i]!=ctx.s[i>>3][i&7]) break;
  if (i==16) {
    fprintf(stderr,
	    "Extended user-supplied information text checksum is valid.\n"
	    "The information text is as follows:\n  > ");
    for(i=0;i<1024-16;i++) {
      if (datablock[i]) fprintf(stderr,"%c",datablock[i]);
      if ((datablock[i]=='\r')||(datablock[i]=='\n')) fprintf(stderr,"  > ");
    }
    fprintf(stderr,"\n");
  } else
    fprintf(stderr,
	    "ERROR: Extended user-supplied information text checksum is wrong:\n");
  
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

  if (argc==10) {
    // Write it

    unsigned char reply[8193];
    int r;

    eeprom_decode_data("Datablock for writing",datablock);
    
    // Use <addr>!g!y<data>!w sequence to write each 16 bytes
    char cmd[1024];
    int address;
    int problems=0;
    fprintf(stderr,"Writing data to EEPROM"); fflush(stderr);
    for(address=0;address<0x800;address+=0x10) {

      int address_not_yet_set=5;
      while(address_not_yet_set)
	{
	  write_radio(fd,(unsigned char *)"!y",2);
	  usleep(10000);
      	  snprintf(cmd,1024,"!C%x!g",address);
	  write_radio(fd,(unsigned char *)cmd,strlen(cmd));
	  usleep(15000);
	  
	  int a;
	  r=read(fd,reply,8192); reply[8192]=0;
	  if (sscanf((char *)reply,"EPRADDR=$%x",&a)!=1) {
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
      }
      write_radio(fd,(unsigned char *)"!w",2);
      usleep(100000);
      // Check for "EEPROM WRITTEN" or "WRITE ERROR"
      
      // clear out any queued data first
      r=read(fd,reply,8192); reply[8192]=0;
      if (strstr((char *)reply,"WRITE ERROR")) {
	fprintf(stderr,"\nERROR: Write error writing to EEPROM @ 0x%x\n",address);
	problems++;
      }
      if (!strstr((char *)reply,"EEPROM WRITTEN")) {
	fprintf(stderr,"\nERROR: No write confirmation received from EEPROM @ 0x%x\n",address);
	problems++;
      }
      
      fprintf(stderr,"."); fflush(stderr);
    }
    fprintf(stderr,"\n");
    if (problems)
      fprintf(stderr,
	      "WARNING: A total of %d problems occurred during writing.\n",problems);
  }
  
  // Verify it
  // Use <addr>!g, !E commands to read out EEPROM data
  unsigned char verifyblock[2048];

  char cmd[1024];
  int address;
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
