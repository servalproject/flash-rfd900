/*
  Debug link between Mesh Extender and RFD900 radio.
  This program is designed to work with a widget board that
  intermediates the serial path between a Mesh Extender and
  power/radio cable.  That cable has the RX and TX lines from
  the Domino Core and RFD900 exposed, so our widget board shims
  in between, and this program relays communications over those
  links, and reports what is happening on the links.

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
#include <errno.h>
#include "flash900.h"

char name1[1024];
char name2[1024];

int output_text_from(char *name,unsigned char *buffer)
{
  int i;
  int start_of_line=1;
  for(i=0;buffer[i];i++) {
    if (buffer[i]=='\r'||buffer[i]=='\n')
      {
	if (start_of_line||(buffer[i]=='\n')) {
	  if (buffer[i-1]=='\n') printf(">>> %s:",name);
	  else printf("\n");
	}
	start_of_line=1;
      }
    else {
      if (start_of_line) printf(">>> %s: ",name);
      start_of_line=0;
      if (buffer[i]!=0x08) printf("%c",buffer[i]); else printf("<BACKSPACE>");
    }
  }
  if (!start_of_line) printf("\n");
  else printf(">>> %s:\n",name);
  return 0;
}

int relay_between(int fd1,int fd2,char *name)
{
  unsigned char buffer[8192];
  int r;
  r=read(fd1,buffer,8192);
  if (r>0) {
    int non_printable=0;
    int i;
    for(i=0;i<r;i++) {
      if (buffer[i]<0x20&&(buffer[i]!=0x0a&&buffer[i]!=0x09&&buffer[i]!=0x0d&&buffer[i]!=0x08&&buffer[i]!=0x07))
	non_printable++;
      if (buffer[i]>0x7d) non_printable++;
    }

    if (non_printable) {
      debug=1;
      dump_bytes(name,buffer,r);
    } else {
      buffer[r]=0;
      output_text_from(name,buffer);
    }
    
    write(fd1,buffer,r);
  }
  return 0;
}

int link_debug(char *port1,char *port2)
{
  int fd1=open(port1,O_RDWR);
  int fd2=open(port2,O_RDWR);

  strcpy(name1,port1);
  strcpy(name2,port2);
  
  if (fd1==-1) {
    fprintf(stderr,"Could not open serial port '%s'\n",port1);
    exit(-1);
  }
  if (set_nonblock(fd1)) {
    fprintf(stderr,"Could not set serial port '%s' non-blocking\n",port1);
    exit(-1);
  }
  if (fd2==-1) {
    fprintf(stderr,"Could not open serial port '%s'\n",port2);
    exit(-1);
  }
  if (set_nonblock(fd2)) {
    fprintf(stderr,"Could not set serial port '%s' non-blocking\n",port2);
    exit(-1);
  }

  // XXX - Need to make speed change and follow based on what we see
  setup_serial_port(fd1,230400);
  setup_serial_port(fd2,230400);

  int count=0;
  
  while(1) {
    relay_between(fd1,fd2,name1);
    relay_between(fd2,fd1,name2);
    
    usleep(1000);
    count++; if (count>1000) {
      count=0;
    }
  }
  
  
}
