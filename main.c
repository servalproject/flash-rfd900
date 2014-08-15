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
#include <strings.h>
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

int main(int argc,char **argv)
{
  if (argc!=3) {
    fprintf(stderr,"usage: flash900 <firmware> <serial port>\n");
    exit(-1);
  }
  
  ihex_recordset_t *ihex=ihex_rs_from_file(argv[1]);
  if (!ihex) {
    fprintf(stderr,"Could not read intelhex from file '%s'\n",argv[1]);
    exit(-2);
  }
  printf("Read %d records\n",ihex->ihrs_count);

  int i;
  int last_start=-1;
  int last_next=-1;
  for(i=0;i<ihex->ihrs_count;i++)
    {
      if (ihex->ihrs_records[i].ihr_address==last_next)
	{
	  last_next=ihex->ihrs_records[i].ihr_address
	    +ihex->ihrs_records[i].ihr_length;
	  if (0) printf("    appending %d bytes: $%04x - $%04x, start=$%04x\n",
		 ihex->ihrs_records[i].ihr_length,
		 ihex->ihrs_records[i].ihr_address,
		 ihex->ihrs_records[i].ihr_address
		 +ihex->ihrs_records[i].ihr_length,
		 last_start
		 );
	}
      else
	{
	  if (last_start!=-1) {
	    printf("  $%04x-$%04x %d\n",
		   last_start,last_next,last_next-last_start);
	  }
	  last_start=ihex->ihrs_records[i].ihr_address;
	  last_next=ihex->ihrs_records[i].ihr_address
	    +ihex->ihrs_records[i].ihr_length;
	}
    }
  if (last_start!=-1) {
    printf("  $%04x-$%04x %d\n",
		   last_start,last_next,last_next-last_start);
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
    
    int speeds[4]={115200,230400,57600,38400};
    int speed_count=4;

  printf("Trying to get command mode...\n");
  for(i=0;i<speed_count;i++) {
    // set port speed and non-blocking, and disable CTSRTS 
    if (setup_serial_port(fd,speeds[i])) {
      fprintf(stderr,"Could not setup serial port '%s'\n",argv[2]);
      exit(-1);
    }

    // Make sure we have left command mode and bootloader mode
    // 0 = $30 = bootloader reboot command
    write(fd,"0\b\b\b\b\b\b\b\b\b\b\b\b\rATO\r",18);
    sleep(2); // allow 2 sec to reboot if it was in bootloader mode already

    // now try to get to AT command mode
    sleep(1);
    write(fd,"+++",3);
    
    // now wait for upto 3 seconds for "OK" 
    // (really 2.00001 - 3.99999 seconds)
    time_t timeout=time(0)+2+1;
    int state=0;
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
      // got command mode (probably)
      printf("Got OK at %d\n",speeds[i]);

      // try AT&UPDATE
      printf("Switching to boot loader...\n");
      write(fd,"AT&UPDATE\r\n",strlen("AT&UPDATE\r\n"));
      // then switch to 115200 regardless of the speed we were at,
      // since the bootloader always talks 115200
      setup_serial_port(fd,115200);
      time_t timeout=time(0)+2+1;
      while(time(0)<timeout) {
	unsigned char buffer[2];
	int r=read(fd,(char *)buffer,1);
	if (r==1) {
	  printf("Got $%02x\n",buffer[0]);
	}
      }

      break;
    } else {
      printf("Modem doesn't seem to be at %dbps\n",speeds[i]);
    }
    
  }

  return 0;
}
