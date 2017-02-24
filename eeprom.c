#include <stdio.h>
#include <stdlib.h>
#include <sys/fcntl.h>
#include <strings.h>
#include <string.h>
#include "flash900.h"

int eeprom_decode_data(char *msg,unsigned char *datablock)
{
  return 0;
}

int eeprom_parse_output(int fd,unsigned char *datablock)
{
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
  memset(datablock,2048-64,' ');
  memset(&datablock[2048-64],64,0);

  FILE *f;

  if (argc==9) {
    // Read data files and assemble 2KB data block to write
    if (strcmp(userdatafile,"-")) {
      f=fopen(userdatafile,"r");
      if (!f) {
	perror("Could not open user data file.\n");
      }
      fread(&datablock[0],1024,1,f);
      fclose(f);
    }
    if (strcmp(protecteddatafile,"-")) {
      f=fopen(protecteddatafile,"r");
      if (!f) {
	perror("Could not open user data file.\n");
      }
      fread(&datablock[1024],1024-64,1,f);
      fclose(f);
    }
    datablock[2048-32+0]=(txpower>>8);
    datablock[2048-32+1]=(txpower&0xff);
    datablock[2048-32+2]=(airspeed>>8);
    datablock[2048-32+3]=(airspeed&0xff);
    datablock[2048-32+4]=(frequency>>8);
    datablock[2048-32+13]=lock_firmware[0];
    datablock[2048-32+14]=primary_country[0];
    datablock[2048-32+15]=primary_country[1];
    
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
  if (detect_speed(fd)) {
    fprintf(stderr,"Could not detect radio speed and mode. Sorry.\n");
    exit(-1);
  }
  
  if (atmode)
    if (switch_to_online_mode(fd)) {
      fprintf(stderr,"Could not switch to online mode.\n");
      exit(-1);
    }

  eeprom_decode_data("Datablock for writing",datablock);
  
  if (argc==9) {
    // Write it
    // Use <addr>!g!y<data>!w sequence to write each 16 bytes
    char cmd[1024];
    int address;
    for(address=0;address<0x800;address+=0x10) {
      snprintf(cmd,1024,"%d!g!y",address);
      write_radio(fd,(unsigned char *)cmd,strlen(cmd));
      // Now write data bytes, escaping ! as !.
      for(int j=0;j<0x10;j++) {
	if (datablock[address+j]=='!') write_radio(fd,(unsigned char *)"!.",2);
	else write_radio(fd,&datablock[address+j],1);
      }
      write_radio(fd,(unsigned char *)"!W",2);
    }
  }
  
  // Verify it
  // Use <addr>!g, !E commands to read out EEPROM data
  unsigned char verifyblock[2048];

  char cmd[1024];
  int address;
  for(address=0;address<0x800;address+=0x80) {
    snprintf(cmd,1024,"%d!g!E",address);
    write_radio(fd,(unsigned char *)cmd,strlen(cmd));
  }
  eeprom_parse_output(fd,verifyblock);
  
  eeprom_decode_data("Datablock read from EEPROM",verifyblock);
  
  
  return 0;      
}
