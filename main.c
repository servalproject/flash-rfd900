/*
  Stand-alone program to flash RFD900 radios. Intended for use on Mesh Extenders
  and other embedded hosts.

  (C) Serval Project Inc. 2014.

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

*/

#include <stdio.h>
#include <termios.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include "cintelhex.h"

#define NOP		0x00
#define OK		0x10
#define FAILED		0x11
#define INSYNC		0x12
#define EOC		0x20
#define GET_SYNC	0x21
#define GET_DEVICE	0x22
#define CHIP_ERASE	0x23
#define LOAD_ADDRESS	0x24
#define PROG_FLASH	0x25
#define READ_FLASH	0x26
#define PROG_MULTI	0x27
#define READ_MULTI	0x28
#define PARAM_ERASE	0x29
#define REBOOT		0x30

int twentyfourbitaddressing=0;

int set_nonblock(int fd)
{
  int flags;
  if ((flags = fcntl(fd, F_GETFL, NULL)) == -1)
    return -1;
  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
    return -1;
  return 0;
}

int set_block(int fd)
{
  int flags;
  if ((flags = fcntl(fd, F_GETFL, NULL)) == -1)
    return -1;
  if (fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) == -1)
    return -1;
  return 0;
}

int setup_serial_port(int fd, int baud)
{
  struct termios t;

  if (tcgetattr(fd, &t)) return -1;
    
  speed_t baud_rate;
  switch(baud){
  case 0: baud_rate = B0; break;
  case 50: baud_rate = B50; break;
  case 75: baud_rate = B75; break;
  case 110: baud_rate = B110; break;
  case 134: baud_rate = B134; break;
  case 150: baud_rate = B150; break;
  case 200: baud_rate = B200; break;
  case 300: baud_rate = B300; break;
  case 600: baud_rate = B600; break;
  case 1200: baud_rate = B1200; break;
  case 1800: baud_rate = B1800; break;
  case 2400: baud_rate = B2400; break;
  case 4800: baud_rate = B4800; break;
  case 9600: baud_rate = B9600; break;
  case 19200: baud_rate = B19200; break;
  case 38400: baud_rate = B38400; break;
  default:
  case 57600: baud_rate = B57600; break;
  case 115200: baud_rate = B115200; break;
  case 230400: baud_rate = B230400; break;
  }

  if (cfsetospeed(&t, baud_rate)) return -1;    
  if (cfsetispeed(&t, baud_rate)) return -1;

  // 8N1
  t.c_cflag &= ~PARENB;
  t.c_cflag &= ~CSTOPB;
  t.c_cflag &= ~CSIZE;
  t.c_cflag |= CS8;

  t.c_lflag &= ~(ICANON | ISIG | IEXTEN | ECHO | ECHOE);
  /* Noncanonical mode, disable signals, extended
   input processing, and software flow control and echoing */
  
  t.c_iflag &= ~(BRKINT | ICRNL | IGNBRK | IGNCR | INLCR |
		 INPCK | ISTRIP | IXON | IXOFF | IXANY | PARMRK);
  /* Disable special handling of CR, NL, and BREAK.
   No 8th-bit stripping or parity error handling.
   Disable START/STOP output flow control. */
  
  // Enable/disable CTS/RTS flow control
#ifndef CNEW_RTSCTS
  t.c_cflag &= ~CRTSCTS;
#else
  t.c_cflag &= ~CNEW_RTSCTS;
#endif

  // no output processing
  t.c_oflag &= ~OPOST;

  if (tcsetattr(fd, TCSANOW, &t))
    return -1;
   
  set_nonblock(fd);

  return 0;
}

int next_char(int fd)
{
  int w=0;
  time_t timeout=time(0)+10;
  while(time(0)<timeout) {
    unsigned char buffer[2];
    int r=read(fd,(char *)buffer,1);
    if (r==1) {
      // { printf("[%02x]",buffer[0]); fflush(stdout); }
      return buffer[0];
    } else { usleep(1000); w++; }
  }
  return -1;
}

void expect_insync(int fd)
{
  int c=next_char(fd);
  if (c!=INSYNC) {
    fprintf(stderr,"\nFailed to synchronise (saw $%02x instead of $%02x)\n",c,INSYNC);
    write(fd,"0",1);
    exit(-3);
  }
}

void expect_ok(int fd)
{
  if (next_char(fd)!=OK) {
    fprintf(stderr,"\nFailed to receive OK.\n");
    write(fd,"0",1);
    exit(-3);
  }
}

void set_flash_addr(int fd,int addr)
{
  unsigned char cmd[8];
  cmd[0]=LOAD_ADDRESS;
  cmd[1]=addr&0xff;
  cmd[2]=(addr>>8)&0xff;
  cmd[3]=EOC;
  if (twentyfourbitaddressing) {
    cmd[3]=(addr>>16)&0xff;
    cmd[4]=EOC;
  }
  write(fd,cmd,4+twentyfourbitaddressing);

  expect_insync(fd);
  expect_ok(fd);
}

void read_flash(int fd,unsigned char *buffer,int length)
{
  unsigned char cmd[8];
  cmd[0]=READ_MULTI;
  cmd[1]=length;
  cmd[2]=EOC;
  write(fd,cmd,3);

  int i;

  for(i=0;i<length;i++) {
    buffer[i]=next_char(fd);
  }

  expect_insync(fd);
  expect_ok(fd);  
}

void write_flash(int fd,unsigned char *buffer,int length)
{
  unsigned char cmd[8+length];
  cmd[0]=PROG_MULTI;
  cmd[1]=length;
  memcpy(&cmd[2],buffer,length);
  cmd[2+length]=EOC;
  write(fd,cmd,3+length);
  expect_insync(fd);
  expect_ok(fd);  
}

// Bulk read all 64KB of flash for quick comparison, and without USB serial
// delays, and also just with higher efficiency because we can use the bandwidth
// more efficiently.
int read_64kb_flash(int fd,unsigned char buffer[65536])
{
  int a;

  // set read address to $0000
  set_flash_addr(fd,0x0000);

  int l=0xff;

  // really only read 63KB.
  // we pipe-line this to avoid USB serial delays, so the first read happens out
  // here, and subsequent read commands just before reading
  for(a=0;a<0xfc00;a+=0xff) {
    // work out transaction length
    l=0xff;
    if (a+l>0xfbff) 
      { l=0xfbff-a+1; }

    printf("\r$%04x - $%04x",a,a+l-1); fflush(stdout);

    // read data
    read_flash(fd,&buffer[a],l);
  }
  printf("\n");
  return 0;

}

void assemble_ihex(ihex_recordset_t *ihex, unsigned char buffer[65536])
{
  int i,j;
  for(i=0;i<65535;i++) buffer[i]=0xff;
  
  for(i=0;i<ihex->ihrs_count;i++)
    if (ihex->ihrs_records[i].ihr_type==0x04) {
    } else if (ihex->ihrs_records[i].ihr_type==0x00) {
      for(j=0;j<ihex->ihrs_records[i].ihr_length;j++)
	{
	  buffer[ihex->ihrs_records[i].ihr_address+j]
	    =ihex->ihrs_records[i].ihr_data[j];
	}
    }
  return;
}

int compare_ihex_record(const void *a,const void *b)
{
  const ihex_record_t *aa=a;
  const ihex_record_t *bb=b;

  if (aa->ihr_address<bb->ihr_address) return -1;
  if (aa->ihr_address>bb->ihr_address) return 1;
  return 0;
}

ihex_recordset_t *load_firmware(char *base,int id,int freq)
{
  char filename[1024];
  snprintf(filename,1024,"%s-%02X-%02X.ihx",base,id,freq);
  
  printf("Board id = $%02x, freq = $%02x : Will load firmware from '%s'\n",
	 id,freq,filename);
  
  if (id==0x82) {
    twentyfourbitaddressing=1;
    printf("Using 24-bit addressing with this board.\n");
  }
  
  ihex_recordset_t *ihex=ihex_rs_from_file(filename);
  if (!ihex) {
    fprintf(stderr,"Could not read intel hex records from '%s'\n",filename);
    return NULL;
  }
    
  printf("Read %d IHEX records from firmware file\n",ihex->ihrs_count);
  
  // Sort IHEX records into ascending address order so that when we flash 
  // them we don't mess things up by writing the flash data in the wrong order 
  qsort(ihex->ihrs_records,ihex->ihrs_count,sizeof(ihex_record_t),
	compare_ihex_record);

  return ihex;
}

int calculate_hash(unsigned char buffer[65536],
		   int start,int end,
		   unsigned int *h1, unsigned int *h2)
{
  int i;
  
  uint32_t hash1=1,hash2=2;
  uint8_t hibit;
  for(i=start;i<end;i++) {
    hibit=hash1>>31;
    hash1 = hash1 << 1;
    hash1 = hash1 ^ hibit;
    hash1 = hash1 ^ buffer[i+0x400];
    
    hash2 = hash2 + buffer[i+0x400];
  }       		
  printf("HASH=%08x+%08x\n",hash1,hash2);

  *h1=hash1;
  *h2=hash2;
  
  return 0;
}

int write_or_verify_flash(int fd,ihex_recordset_t *ihex,int writeP)
{
  int max=255;
  if (writeP) max=32;

  int address_base=0x00000000;
  
  printf("max=%d\n",max);

  int i;
  int fail=0;
  for(i=0;i<ihex->ihrs_count;i++)
    if (ihex->ihrs_records[i].ihr_type==0x04) {
      // Set upper-16 bits of target address
      address_base=ihex->ihrs_records[i].ihr_data[0]<<24;
      address_base|=ihex->ihrs_records[i].ihr_data[1]<<16;
    } else if (ihex->ihrs_records[i].ihr_type==0x00)
      {
	if (fail) break;

	if ((ihex->ihrs_records[i].ihr_address+ihex->ihrs_records[i].ihr_length)
	    >=0xfc00) {
	  fprintf(stderr,"\nWARNING: Intel hex file contains out of bound data ($%02x-$%04x)\n",
		  ihex->ihrs_records[i].ihr_address,
		  ihex->ihrs_records[i].ihr_address+ihex->ihrs_records[i].ihr_length);
	}
	
	int j;
	// write 32 bytes at a time
	for(j=0;j<ihex->ihrs_records[i].ihr_length;j+=max)
	  {
	    // work out how big this piece is
	    int length=max;
	    if (j+length>ihex->ihrs_records[i].ihr_length) {
	      // printf("  clipping read from $%02x\n",length);
	      length=ihex->ihrs_records[i].ihr_length-j;
	    }
	    
	    printf("\rRange $%04x - $%04x (len=$%02x)\r",
		   ihex->ihrs_records[i].ihr_address+j,
		   ihex->ihrs_records[i].ihr_address+j+length-1,length);
	    fflush(stdout);

	    if (writeP) {
	      // Write to flash
	      set_flash_addr(fd,ihex->ihrs_records[i].ihr_address+j);  
	      write_flash(fd,&ihex->ihrs_records[i].ihr_data[j],length);
	    }
	    
	    // Read back from flash and verify.
	    unsigned char buffer[length];
	    set_flash_addr(fd,ihex->ihrs_records[i].ihr_address+j);  
	    read_flash(fd,buffer,length);
	    int k;
	    for(k=0;k<length;k++)
	      if (ihex->ihrs_records[i].ihr_data[j+k]
		  !=buffer[k])
		{
		  // Verify error
		  fprintf(stderr,"\nVerify error at $%04x"
			  " : expected $%02x, but read $%02x\n",
			  ihex->ihrs_records[i].ihr_address+j+k,
			  ihex->ihrs_records[i].ihr_data[j+k],buffer[k]);
		  fail=1;
		}
	  }
      }
  printf("\n");
  if (fail) {
    if (writeP) {
      write(fd,"0",1);
      exit(-4);
    }
    else return -1;
  }
  return 0;
}
  
long long gettime_ms()
{
  struct timeval nowtv;
  // If gettimeofday() fails or returns an invalid value, all else is lost!
  if (gettimeofday(&nowtv, NULL) == -1)
    return -1;
  if (nowtv.tv_sec < 0 || nowtv.tv_usec < 0 || nowtv.tv_usec >= 1000000)
    return -1;
  return nowtv.tv_sec * 1000LL + nowtv.tv_usec / 1000;
}

int main(int argc,char **argv)
{
  int start=0x0400;
  int end=0xfc00;
  int id=0xff;
  int freq=0xff;
  unsigned int hash1=1,hash2=2;
  ihex_recordset_t *ihex=NULL;

  
  if ((argc<3|| argc>4)
      ||(argc==4&&(strcasecmp(argv[3],"force")
		   &&strcasecmp(argv[3],"verify")))) {
    fprintf(stderr,"usage: flash900 <firmware> <serial port> [force]\n");
    exit(-1);
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
    
    int speeds[8]={230400,115200,57600,38400,19200,9600,2400,1200};
    int speed_count=8;

  printf("Trying to get command mode...\n");
  int i;
  for(i=0;i<speed_count;i++) {
    // set port speed and non-blocking, and disable CTSRTS 
    if (setup_serial_port(fd,speeds[i])) {
      fprintf(stderr,"Could not setup serial port '%s'\n",argv[2]);
      exit(-1);
    }

    int last_char = 0;
    
    // Make sure we have left command mode and bootloader mode
    // 0 = $30 = bootloader reboot command
    unsigned char cmd[260]; bzero(&cmd[0],260);
    
    printf("Checking if stuck in bootloader\n");
    // Make sure there is no command in progress with the boot loader
    write(fd,cmd,260);
    // Try to sync with bootloader if it is already running
    cmd[0]=GET_DEVICE;
    cmd[1]=EOC;
    write(fd,cmd,2);
    unsigned char bootloaderdetect[4]={0x43,0x91,0x12,0x10};
    int state=0;
    long long timeout=gettime_ms()+1250;
    while(gettime_ms()<timeout) {
      unsigned char buffer[2];
      int r=read(fd,buffer,1);
      if (r==1) {
	//	printf("  read %02X\n",buffer[0]);
	if (buffer[0]==bootloaderdetect[state]) state++; else state=0;
	// Also detect RFD900+ bootloader properly
	if ((state==0)&&(buffer[0]==0x82)) state=1;
	if (state==4) {
	  printf("Looks like we are in the bootloader already\n");
	  break;
	}
      }
    }
    
    if (state==4)
      printf("Detected RFD900 is already in bootloader\n");
    else
      {

	printf("Checking if supports !F for fast ID of firmware\n");
	unsigned char reply[257];
	// clear out any queued data first
	int r=read(fd,reply,256); reply[256]=0;
	// send !F
	write(fd,"!F",2);
	usleep(200000);
	read(fd,reply,256); reply[256]=0;
	if (r>0&&r<256) reply[r]=0;
	printf("!F reply is '%s'\n",reply);
	// if !F we are probably in command mode
	// if HASH=xx:xx:xxxx:xxxx:xxxx+xxxx, then firmware supports function
	if (sscanf((const char *)reply,"HASH=%x:%x:%x:%x:%x+%x",
		   &id,&freq,&start,&end,&hash1,&hash2)==6) {
	  printf("Successfully parsed HASH response.\n");
	  ihex=load_firmware(argv[1],id,freq);

	  unsigned int newhash1,newhash2;
	  unsigned char ibuffer[65536];
	  assemble_ihex(ihex,ibuffer);
	  calculate_hash(ibuffer,start,end,&newhash1,&newhash2);

	  exit(0);
	}

	
	printf("Trying to switch to AT command mode\n");
	write(fd,"\b\b\b\b\b\b\b\b\b\b\b\b\r",14);
	// give it time to process the above, so that the first character of ATO
	// doesn't get eaten.
	usleep(10000);        
	write(fd,"ATO\r",4);
	// sleep(2); // allow 2 sec to reboot if it was in bootloader mode already
	
	// now try to get to AT command mode
	sleep(1);
	write(fd,"+++",3);
	
	// now wait for upto 1.2 seconds for "OK" 
	timeout=gettime_ms()+1200;
	state=0;
	while(gettime_ms()<timeout) {
	  char buffer[1];
	  int r=read(fd,buffer,1);
	  if (r==1) {
	    //	    printf("  read %02X\n",buffer[0]);
	    if ((buffer[0]=='K') && (last_char == 'O')) state=2; else state=0;
	    last_char = buffer[0];
	    if (state==2) break;
	  } else usleep(10000);
	}
	if (state==2) {	 
	  // try AT&UPDATE or ATS1=115\rAT&W\rATZ if the modem isn't already on 115200bps
	  printf("Switching to boot loader...\n");
	  char *cmd="AT&UPDATE\r\n";

	  if (speeds[i]==115200) {
	    write(fd,cmd,strlen(cmd));
	  } else {
	    char *cmd="ATS1=115\r\n";
	    write(fd,cmd,strlen(cmd));
	    sleep(1);
	    cmd="AT&W\r\n";
	    write(fd,cmd,strlen(cmd));
	    sleep(1);
	    cmd="ATZ\r\n";	  
	    write(fd,cmd,strlen(cmd));
	    sleep(1);

	    // Go back to looking for modem at 115200
	    printf("Changing modem from %d to 115200bps\n",
		   speeds[i]);
	    i=-1; continue;
	  }
	  
	  // then switch to 115200 regardless of the speed we were at,
	  // since the bootloader always talks 115200
	  setup_serial_port(fd,115200);
      
	  // give time to switch to boot loader
	  // and consume any characters that arrive in the meantime
	  timeout=gettime_ms()+1500;
	  while(gettime_ms()<timeout) {
	    unsigned char buffer[2];
	    int r=read(fd,(char *)buffer,1);
	    // fprintf(stderr,"Read %02X from bootloader.\n",buffer[0]);
	    if (r!=1) usleep(100000); else {
	      // printf("  read %02X\n",buffer[0]);
	    }
	  }
      
	  state=4;
	}
      }
    if (state==4) {
      // got command mode (probably)
      printf("Got OK at %d\n",speeds[i]);
      
      // ask for board ID
      unsigned char cmd[1024];
      cmd[0]=GET_DEVICE;
      cmd[1]=EOC;
      write(fd,cmd,2);

      id = next_char(fd);
      freq = next_char(fd);
      expect_insync(fd);
      expect_ok(fd);
      
      ihex=load_firmware(argv[1],id,freq);
      if (!ihex) {
	
	// Reboot radio
	write(fd,"0",1);
	
	exit(-2);
      }

      
      int i;
      if (0)
	for(i=0;i<ihex->ihrs_count;i++)
	  printf("$%04x - $%04x\n",
		 ihex->ihrs_records[i].ihr_address,
		 ihex->ihrs_records[i].ihr_address+
		 ihex->ihrs_records[i].ihr_length-1);
      
      
      // Reset parameters
      cmd[0]=PARAM_ERASE;
      cmd[1]=EOC;
      write(fd,cmd,2);
      expect_insync(fd);
      expect_ok(fd);

      printf("Erased parameters.\n");

      // Program all parts of the firmware and verify that that got written
      printf("Checking if the radio already has this version of firmware...\n");
      int fail=0;
      int force=0;
      int verify=0;
      if (argc>3) {
	if (!strcasecmp(argv[3],"force")) force=1;
	if (!strcasecmp(argv[3],"verify")) verify=1;
      }

      /*
	XXX - We only support 64KB of flash, even though the RFD900+ has 128KB
      */
      
      if (!force) {
	// read flash and compare with ihex records
	unsigned char buffer[65536];
	printf("Bulk reading from flash...\n");
	read_64kb_flash(fd,buffer);
	printf("Read all 64KB flash. Now verifying...\n");
	unsigned int newhash1,newhash2;
	unsigned char ibuffer[65536];
	assemble_ihex(ihex,ibuffer);
	calculate_hash(ibuffer,start,end,&newhash1,&newhash2);

	// Only check $0000-$F7FD, as the rest is boot loader or other stuff.
	// (Is $F7FE-$F7FF for non-volatile variable storage or something?
	// it seems to never be set right after restart, but verifies back fine
	// when actually writing).
	// XXX - Actually, the AT&UPDATE command clears those bytes, so they will
	// always be wrong, but the radio won't boot until they are set again.
	// So we are stuck with this, until we can add commands to our CSMA firmware to
	// allow reading of FLASH memory without entering the bootloader.
	int i;
	for(i=0;i<ihex->ihrs_count;i++)
	  if (ihex->ihrs_records[i].ihr_type==0x00)
	    // if (ihex->ihrs_records[i].ihr_address<0xF7FE)
	      {
		if (fail&&(!verify)) break;
		
		if (memcmp(&buffer[ihex->ihrs_records[i].ihr_address],
			   ihex->ihrs_records[i].ihr_data,
			   ihex->ihrs_records[i].ihr_length)) {
		  fail=1;
		  if (verify) {
		    printf("Verify error in range $%04x - $%04x\n",
			   ihex->ihrs_records[i].ihr_address,
			   ihex->ihrs_records[i].ihr_address
			   +ihex->ihrs_records[i].ihr_length-1);
		    {
		      int j;
		      printf("Expected content:");
		      for(j=0;j<ihex->ihrs_records[i].ihr_length;j++)
			printf(" %02x",ihex->ihrs_records[i].ihr_data[j]);
		      printf("\n");
		      printf("Read from flash:");
		      for(j=0;j<ihex->ihrs_records[i].ihr_length;j++)
			printf(" %02x",buffer[ihex->ihrs_records[i].ihr_address+j]);
		      printf("\n");
		    }
		  }
		}
	      }
	
      }
      if ((force||fail)&&(!verify))
	{
	  printf("\nFirmware differs: erasing and flashing...\n");

	  // Erase ROM
	  printf("Erasing flash.\n");
	  cmd[0]=CHIP_ERASE;
	  cmd[1]=EOC;
	  write(fd,cmd,2);
	  expect_insync(fd);
	  expect_ok(fd);

	  // Write ROM
	  printf("Flash erased, now writing new firmware.\n");
	  write_or_verify_flash(fd,ihex,1);
	}

      // Reboot radio
      cmd[0]='0';
      write(fd,cmd,1);

      break;
    } else {
      printf("Modem doesn't seem to be at %dbps\n",speeds[i]);
    }
    
  }

  return 0;
}
