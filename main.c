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
      // if (w) { printf("[%d]",w); fflush(stdout); }
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
  write(fd,cmd,4);

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

int write_or_verify_flash(int fd,ihex_recordset_t *ihex,int writeP)
{
  int max=255;
  if (writeP) max=32;

  printf("max=%d\n",max);

  int i;
  int fail=0;
  for(i=0;i<ihex->ihrs_count;i++)
    if (ihex->ihrs_records[i].ihr_type==0x00)
      {
	if (fail) break;

	int j;
	// write 32 bytes at a time
	for(j=0;j<ihex->ihrs_records[i].ihr_length;j+=max)
	  {
	    // work out how big this piece is
	    int length=max;
	    if (j+length>ihex->ihrs_records[i].ihr_length) {
	      printf("  clipping read from $%02x\n",length);
	      length=ihex->ihrs_records[i].ihr_length-j;
	    }
	    
	    printf("\rRange $%04x - $%04x (len=$%02x)",
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
  
int compare_ihex_record(const void *a,const void *b)
{
  const ihex_record_t *aa=a;
  const ihex_record_t *bb=b;

  if (aa->ihr_address<bb->ihr_address) return -1;
  if (aa->ihr_address>bb->ihr_address) return 1;
  return 0;
}

int main(int argc,char **argv)
{
  if ((argc<3|| argc>4)
      ||(argc==4&&strcasecmp(argv[3],"force"))) {
    fprintf(stderr,"usage: flash900 <firmware> <serial port> [force]\n");
    exit(-1);
  }

  ihex_recordset_t *ihex=ihex_rs_from_file(argv[1]);
  if (!ihex) {
    fprintf(stderr,"Could not read intelhex from file '%s'\n",argv[1]);
    exit(-2);
  }
  printf("Read %d IHEX records from firmware file\n",ihex->ihrs_count);

  // Sort IHEX records into ascending address order so that when we flash 
  // them we don't mess things up by writing the flash data in the wrong order 
  qsort(ihex->ihrs_records,ihex->ihrs_count,sizeof(ihex_record_t),
	compare_ihex_record);

  int i;
  if (0)
    for(i=0;i<ihex->ihrs_count;i++)
      printf("$%04x - $%04x\n",
	     ihex->ihrs_records[i].ihr_address,
	     ihex->ihrs_records[i].ihr_address+
	     ihex->ihrs_records[i].ihr_length-1);

  int fd=open(argv[2],O_RDWR);
  if (fd==-1) {
    fprintf(stderr,"Could not open serial port '%s'\n",argv[2]);
    exit(-1);
  }
    if (set_nonblock(fd)) {
      fprintf(stderr,"Could not set serial port '%s' non-blocking\n",argv[2]);
      exit(-1);
    }
    
    int speeds[8]={115200,230400,57600,38400,19200,9600,2400,1200};
    int speed_count=8;

  printf("Trying to get command mode...\n");
  for(i=0;i<speed_count;i++) {
    // set port speed and non-blocking, and disable CTSRTS 
    if (setup_serial_port(fd,speeds[i])) {
      fprintf(stderr,"Could not setup serial port '%s'\n",argv[2]);
      exit(-1);
    }

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
    time_t timeout=time(0)+2;
    while(time(0)<timeout) {
      unsigned char buffer[2];
      int r=read(fd,buffer,1);
      if (r==1) {
	if (buffer[0]==bootloaderdetect[state]) state++; else state=0;
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
	printf("Trying to switch to AT command mode\n");
	write(fd,"\b\b\b\b\b\b\b\b\b\b\b\b\rATO\r",18);
	sleep(2); // allow 2 sec to reboot if it was in bootloader mode already
	
	// now try to get to AT command mode
	sleep(1);
	write(fd,"+++",3);
	
	// now wait for upto 3 seconds for "OK" 
	// (really 2.00001 - 3.99999 seconds)
	timeout=time(0)+2+1;
	state=0;
	char *ok_string="OK";
	while(time(0)<timeout) {
	  char buffer[2];
	  int r=read(fd,buffer,1);
	  if (r==1) {
	    if (buffer[0]==ok_string[state]) state++; else state=0;
	    if (state==2) break;
	  } else usleep(50000);
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
	  time_t timeout=time(0)+2;
	  while(time(0)<timeout) {
	    unsigned char buffer[2];
	    int r=read(fd,(char *)buffer,1);
	    if (r!=1) usleep(100000);
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

      int id = next_char(fd);
      int freq = next_char(fd);
      expect_insync(fd);
      expect_ok(fd);
     
      printf("Board id = $%02x, freq = $%02x\n",id,freq);

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
      if (argc==3) {
	// read flash and compare with ihex records
	unsigned char buffer[65536];
	printf("Bulk reading from flash...\n");
	read_64kb_flash(fd,buffer);
	printf("Read all 64KB flash. Now verifying...\n");
	
	int i;
	for(i=0;i<ihex->ihrs_count;i++)
	  if (ihex->ihrs_records[i].ihr_type==0x00)
	    {
	      if (fail) break;
	      
	      if (memcmp(&buffer[ihex->ihrs_records[i].ihr_address],
			 ihex->ihrs_records[i].ihr_data,
			 ihex->ihrs_records[i].ihr_length))
		fail=1;
	    }
	
      }
      if ((argc==4)||fail)
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
      write(fd,"0",1);

      break;
    } else {
      printf("Modem doesn't seem to be at %dbps\n",speeds[i]);
    }
    
  }

  return 0;
}
