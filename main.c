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
#include "flash900.h"

int twentyfourbitaddressing=0;

long long gettime_ms();

long long last_write_time=0;
long long latency=0;

int verify_against_buffer(ihex_recordset_t *ihex,unsigned char *buffer, int verbose);

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

int last_baud=115200;
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

  fprintf(stderr,"Set serial port to %dbps\n",baud);

  last_baud=baud;
  
  return 0;
}

int next_char(int fd)
{
  fsync(fd);
  int w=0;
  time_t timeout=time(0)+10;
  while(time(0)<timeout) {
    unsigned char buffer[2];
    int r=read(fd,(char *)buffer,1);
    if (r==1) {
      // { printf("[%02x]",buffer[0]); fflush(stdout); }
      if (last_write_time) {
	latency=gettime_ms()-last_write_time;
	last_write_time=0;
	// fprintf(stderr,"serial latency = %lldms\n",latency);
      }
      
      return buffer[0];
    } else { usleep(1000); w++; }
  }
  return -1;
}

#define expect_insync(X) _expect_insync(fd,__FILE__,__LINE__);
#define expect_ok(X) _expect_ok(fd,__FILE__,__LINE__);

void _expect_insync(int fd,char *file,int line)
{
  fsync(fd);
  int c=next_char(fd);
  if (c!=INSYNC) {
    fprintf(stderr,"\nFailed to synchronise (saw $%02x instead of $%02x)\n",c,INSYNC);
    write(fd,"0",1);
    exit(-3);
  }
}

void _expect_ok(int fd,char *file,int line)
{
  fsync(fd);
  if (next_char(fd)!=OK) {
    fprintf(stderr,"\nFailed to receive OK.\n");
    write(fd,"0",1);
    exit(-3);
  }

}

void set_flash_addr_async(int fd,int addr)
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
  last_write_time=gettime_ms();
}

void set_flash_addr(int fd,int addr)
{
  set_flash_addr_async(fd,addr);
  expect_insync(fd);
  expect_ok(fd);
}

void request_flash_read(int fd,unsigned char *buffer,int length)
{
  unsigned char cmd[8];
  cmd[0]=READ_MULTI;
  cmd[1]=length;
  cmd[2]=EOC;
  write(fd,cmd,3);
  last_write_time=gettime_ms();
}
 
void flash_read_requested_bytes(int fd,unsigned char *buffer, int length)
{
  int i;
  
  for(i=0;i<length;i++) {
    buffer[i]=next_char(fd);
  }
  expect_insync(fd);
  expect_ok(fd);  
}
 
void read_flash(int fd,unsigned char *buffer,int length)
{
  request_flash_read(fd,buffer,length);
  
  int i;

  for(i=0;i<length;i++) {
    buffer[i]=next_char(fd);
  }
  expect_insync(fd);
  expect_ok(fd);  
}


 
void write_flash_async(int fd,unsigned char *buffer,int length)
{
  unsigned char cmd[8+length];
  cmd[0]=PROG_MULTI;
  cmd[1]=length;
  memcpy(&cmd[2],buffer,length);
  cmd[2+length]=EOC;
  write(fd,cmd,3+length);
  last_write_time=gettime_ms();
}

void write_flash(int fd,unsigned char *buffer,int length)
{
  write_flash_async(fd,buffer,length);
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

  // We can't request all reads at once, but we can do 4 at a time.

  // Read 0xfc bytes, so that we have exactly 256 reads to schedule.
  // This makes it easier for us to pipeline things.
  for(a=0;a<0xfc00;a+=(0xfc*4)) {
    // work out transaction length
    l=0xfc;

    printf("\r$%04x - $%04x",a,a+l*4-1); fflush(stdout);

    request_flash_read(fd,&buffer[a],l);
    // sleep for 270 character transfers to allow read command to be fully
    // processed, before dispatching the next, as the bootloader has no
    // input buffer
    usleep(270*100);
    request_flash_read(fd,&buffer[a],l);
    usleep(270*100);
    request_flash_read(fd,&buffer[a],l);
    usleep(270*100);
    request_flash_read(fd,&buffer[a],l);

    // read data
    flash_read_requested_bytes(fd,&buffer[a],l);
    flash_read_requested_bytes(fd,&buffer[a+1*0xfc],l);
    flash_read_requested_bytes(fd,&buffer[a+2*0xfc],l);
    flash_read_requested_bytes(fd,&buffer[a+3*0xfc],l);
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
      // XXX Set upper 16-bits of address
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
  ihex_aggregate_records(ihex);
  printf("(%d records remain after aggregation)\n",ihex->ihrs_count);
  
  // Sort IHEX records into ascending address order so that when we flash 
  // them we don't mess things up by writing the flash data in the wrong order 
  qsort(ihex->ihrs_records,ihex->ihrs_count,sizeof(ihex_record_t),
	compare_ihex_record);

  return ihex;
}

int calculate_hash(unsigned char buffer[65536],unsigned int checksums[64],
		   int start,int end,
		   unsigned int *h1, unsigned int *h2)
{
  int i;
  
  uint32_t hash1=1,hash2=2;
  uint8_t hibit;
  uint8_t	j=0;
  for(i=start;i<end;i++) {
    hibit=hash1>>31;
    hash1 = hash1 << 1;
    hash1 = hash1 ^ hibit;
    hash1 = hash1 ^ buffer[i];
    
    hash2 = hash2 + buffer[i];

    if((i&0x3ff)==0x0) {
      j++; if ((j<64)) checksums[j]=buffer[i];
      checksums[0]++;
    } else {
      if ((j<64)) checksums[j] += buffer[i];
    }

  }

  for(j=0;j<64;j++) checksums[j] &= 0xffff;
  
  printf("HASH=%08x+%08x\n",hash1,hash2);

  *h1=hash1;
  *h2=hash2;
  
  return 0;
}

int write_64kb(char *filename,unsigned char *buffer)
{
  FILE *f=fopen(filename,"w");
  fwrite(buffer, 65536, 1, f);
  fclose(f);
  return 0;
}

int read_64kb(char *filename,unsigned char *buffer)
{
  FILE *f=fopen(filename,"r");
  fread(buffer, 65536, 1, f);
  fclose(f);
  return 0;
}


int write_to_flash(int fd,ihex_recordset_t *ihex,int writeP)
{
  int max=255;
  if (writeP) max=64;

  int address_base=0x00000000;
  
  printf("max=%d\n",max);

  int i;
  int fail=0;
  int last_flash_address=-1;
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
	// write 32 bytes at a time.
	// (but do all writing before verification, so that we avoid additional
	// USB serial latencies by continually adjusting the flash address.  This
	// also allows us to verify 255 bytes at a time, instead of just 32.)
	if (writeP) {
	  if (last_flash_address!=ihex->ihrs_records[i].ihr_address) {
	    set_flash_addr(fd,ihex->ihrs_records[i].ihr_address);
	    last_flash_address=ihex->ihrs_records[i].ihr_address;
	  }
	  for(j=0;j<ihex->ihrs_records[i].ihr_length;j+=max)
	    {
	      // work out how big this piece is
	      int length=max;
	      if (j+length>ihex->ihrs_records[i].ihr_length) {
		// printf("  clipping read from $%02x\n",length);
		length=ihex->ihrs_records[i].ihr_length-j;
	      }
	      
	      printf("\rWrite $%04x - $%04x (len=$%02x)     \r",
		     ihex->ihrs_records[i].ihr_address+j,
		     ihex->ihrs_records[i].ihr_address+j+length-1,length);
	      fflush(stdout);
	      
	      // Write to flash
	      write_flash_async(fd,&ihex->ihrs_records[i].ihr_data[j],length);
	      expect_insync(fd); expect_ok(fd);
	      last_flash_address+=length;
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
  printf("Version 20160621.1359.1\n");
  
  int fail=0;
  int force=0;
  int verify=0;
  if (argc>3) {
    if (!strcasecmp(argv[3],"force")) force=1;
    if (!strcasecmp(argv[3],"verify")) verify=1;
  }
  
  int start=0x0400;
  int end=0xfc00;
  int id=0xff;
  int freq=0xff;
  unsigned int hash1=1;
  ihex_recordset_t *ihex=NULL;

  int exit_speed=0;

  long long lap_time=gettime_ms();
  long long read_time=0;
  long long write_time=0;
  long long verify_time=0;
  long long modem_time=0;

  if ((argc>4)&&(!strcasecmp(argv[4],"debug")))
    {
      argc--;
      debug=1;
    }

  if (!strcmp(argv[1],"eeprom")) {
    if (argc!=8) {
      fprintf(stderr,"usage: flash900 eeprom <serial port> <user data file> <protected data file> <frequency> <txpower> <airspeed>\n");
      exit(-1);
    }
    char *userdatafile=argv[3];
    
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

      
      
      return 0;      
  }
  
  
  if ((argc<3|| argc>4)
      ||(argc==4&&(strcasecmp(argv[3],"force")
		   &&strcasecmp(argv[3],"verify")
		   &&strcasecmp(argv[3],"230400")
		   &&strcasecmp(argv[3],"115200")
		   &&strcasecmp(argv[3],"57600")
		   )
	 )) {
    fprintf(stderr,"usage: flash900 <firmware> <serial port> [force|verify|230400|115200|57600]\n");
    exit(-1);
  }

  if (argc==4) exit_speed=atoi(argv[3]);

  int fd=open(argv[2],O_RDWR);
  if (fd==-1) {
    fprintf(stderr,"Could not open serial port '%s'\n",argv[2]);
    exit(-1);
  }
  if (set_nonblock(fd)) {
    fprintf(stderr,"Could not set serial port '%s' non-blocking\n",argv[2]);
    exit(-1);
  }

  printf("Trying to detect speed and mode...\n");
  lap_time=gettime_ms();

  if (force) {
    // Try using !Cup!B command to drop direct to bootloader
    try_bang_B(fd);
  }
  

  
  if (detect_speed(fd)) {
    fprintf(stderr,"Could not detect radio speed and mode. Sorry.\n");
    exit(-1);
  }

  // Radio is now at detectedspeed bps.
  fprintf(stderr,"Detected radio speed and mode.\n");
             
  unsigned char cmd[260]; 

  if (bootloadermode)
    printf("Detected RFD900 is already in bootloader -- we have no choice but to reflash to exit boot loader mode.\n");
  else 
    {
      if (atmode) {
	// Switch to online mode to ask the radio what firmware version it has
	switch_to_online_mode(fd);
      }
      
      printf("Checking if radio supports !F for fast ID of firmware\n");
      unsigned char reply[8193];
      unsigned int checksum[64];
      
      // clear out any queued data first
      int r=read(fd,reply,8192); reply[8192]=0;
      // send !F
      write(fd,"!F",2);
      usleep(200000);
      r=read(fd,reply,8192); reply[8192]=0;
      if (r>0&&r<8192) reply[r]=0;
      printf("!F reply is '%s'\n",reply);
      // if !F we are probably in command mode
      // if HASH=xx:xx:xxxx:xxxx:xxxx+xxxx, then firmware supports function
      int fields
	= sscanf((const char *)reply,"HASH=%x:%x:%x:%x:%x,"
		 "%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,"
		 "%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,"
		 "%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,"
		 "%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x",
		 &id,&freq,&start,&end,&hash1,
		 &checksum[0x00],&checksum[0x01],&checksum[0x02],&checksum[0x03],
		 &checksum[0x04],&checksum[0x05],&checksum[0x06],&checksum[0x07],
		 &checksum[0x08],&checksum[0x09],&checksum[0x0a],&checksum[0x0b],
		 &checksum[0x0c],&checksum[0x0d],&checksum[0x0e],&checksum[0x0f],
		 &checksum[0x10],&checksum[0x11],&checksum[0x12],&checksum[0x13],
		 &checksum[0x14],&checksum[0x15],&checksum[0x16],&checksum[0x17],
		 &checksum[0x18],&checksum[0x19],&checksum[0x1a],&checksum[0x1b],
		 &checksum[0x1c],&checksum[0x1d],&checksum[0x1e],&checksum[0x1f],
		 &checksum[0x20],&checksum[0x21],&checksum[0x22],&checksum[0x23],
		 &checksum[0x24],&checksum[0x25],&checksum[0x26],&checksum[0x27],
		 &checksum[0x28],&checksum[0x29],&checksum[0x2a],&checksum[0x2b],
		 &checksum[0x2c],&checksum[0x2d],&checksum[0x2e],&checksum[0x2f],
		 &checksum[0x30],&checksum[0x31],&checksum[0x32],&checksum[0x33],
		 &checksum[0x34],&checksum[0x35],&checksum[0x36],&checksum[0x37],
		 &checksum[0x38],&checksum[0x39],&checksum[0x3a],&checksum[0x3b],
		 &checksum[0x3c],&checksum[0x3d],&checksum[0x3e],&checksum[0x3f]
		 );
      printf("Found %d fields @ %dbps.\n",fields,detectedspeed);
      
      if (fields==(5+64)) {
	printf("Successfully parsed HASH response.\n");
	if (first_speed==-1) first_speed=detectedspeed;
	
	ihex=load_firmware(argv[1],id,freq);
	
	unsigned int newhash1,newhash2;
	unsigned char ibuffer[65536];
	unsigned int ichecksums[64];
	assemble_ihex(ihex,ibuffer);
	calculate_hash(ibuffer,ichecksums,0x400,0xf800,&newhash1,&newhash2);
	
	// Only check the first 60KB, as the rest is bootloader and other stuff
	// that we can't rely upon.  This leaves the chance of some possible changes
	// not getting picked up -- however, since this method only applies to
	// the Serval Project, we can manage that risk there.
	// Don't check the first 4KB: It changes (is this where parameters get stored?)
	int different=0;
	int i;
	for(i=1;i<60;i++) {
	  if (checksum[i]!=ichecksums[i]) {
	    printf("Checksum for $%04x - $%04x does not match ($%04x vs $%04x)\n",
		   i*0x400,(i+1)*0x400-1,checksum[i],ichecksums[i]);
	    different++;
	  }
	}

	if ((!different)&&(!force)) {
	  printf("Flash ROM matched via checksum: nothing to do.\n");
	  // ... except to make sure that the modem is set back to default speed
	  if (exit_speed<=0) 
	    // switch radio speed and reboot	      
	    change_radio_to(fd,230400);
	  else
	    change_radio_to(fd,exit_speed);
	    
	  exit(0);
	}
	if (different) force=1;
      }

      if (switch_to_bootloader(fd)) {
	fprintf(stderr,"Failed to enter boot loader.\n");
	exit(-1);
      }	
    }

  modem_time=gettime_ms()-lap_time; lap_time=gettime_ms();
  
  // Radio has incorrect or unknown firmware version, and is already in bootloader
  // mode for us.  Proceed with updating it.
  
  // ask for board ID
  cmd[0]=GET_DEVICE;
  cmd[1]=EOC;
  write(fd,cmd,2);
    
  id = next_char(fd);
  freq = next_char(fd);
  expect_insync(fd);
  expect_ok(fd);
    
  ihex=load_firmware(argv[1],id,freq);
  if (!ihex) {

    fprintf(stderr,"Sorry, I don't have firmware for your model of radio.  Your radio is probably stuck in bootloader mode now, until you find another way to update it.\n");
      
    // Reboot radio
    write(fd,"0",1);
      
    exit(-2);
  }
        
    
  // Reset parameters
  cmd[0]=PARAM_ERASE;
  cmd[1]=EOC;
  write(fd,cmd,2);
  expect_insync(fd);
  expect_ok(fd);
    
  printf("Erased parameters.\n");
    
  // Program all parts of the firmware and verify that that got written
  printf("Checking if the radio already has this version of firmware...\n");
    
  /*
    XXX - We only support 64KB of flash, even though the RFD900+ has 128KB
  */
    
  if (!force) {
    // read flash and compare with ihex records
    unsigned char buffer[65536];
    printf("Bulk reading from flash...\n");    
    read_64kb_flash(fd,buffer);
    read_time=gettime_ms()-lap_time; lap_time=gettime_ms();
    // write_64kb("fromradio.bin",buffer);
    printf("Read all 64KB flash. Now verifying...\n");
    unsigned int newhash1,newhash2;
    unsigned char ibuffer[65536];
    unsigned int ichecksums[64];
    assemble_ihex(ihex,ibuffer);
    // write_64kb("fromhex.bin",ibuffer);
    calculate_hash(ibuffer,ichecksums,start,end,&newhash1,&newhash2);

    fail=verify_against_buffer(ihex,buffer,1);
  }
  if ((force||fail)&&(!verify))
    {
      printf("\nFirmware differs: erasing and flashing...\n");

      lap_time=gettime_ms();

      // Erase ROM
      printf("Erasing flash.\n");
      cmd[0]=CHIP_ERASE;
      cmd[1]=EOC;
      write(fd,cmd,2);
      expect_insync(fd);
      expect_ok(fd);

      // Write ROM
      printf("Flash erased, now writing new firmware.\n");
      write_to_flash(fd,ihex,1);
      write_time=gettime_ms()-lap_time; lap_time=gettime_ms();

      // Verify that we wrote it correctly
      unsigned char buffer[65536];
      printf("Verifying new firmware.\n");
      read_64kb_flash(fd,buffer);
      verify_against_buffer(ihex,buffer,1);
      verify_time=gettime_ms()-lap_time; lap_time=gettime_ms();

    }

  // Reboot radio
  cmd[0]='0';
  write(fd,cmd,1);
  printf("Radio rebooted.\n");

  printf("Time breakdown: \n  Modem control = %lldms,  flash read = %lldms,\n  flash write = %lldms,  flash verify =  %lldms.\n  TOTAL = %lld ms.\n",
	 modem_time,read_time,write_time,verify_time,
	 modem_time+read_time+write_time+verify_time);
	 
  
  return 0;
}

int verify_against_buffer(ihex_recordset_t *ihex,unsigned char *buffer, int verbose)
{
  int i;

  // Only check $0000-$F7FD, as the rest is boot loader or other stuff.
  // (Is $F7FE-$F7FF for non-volatile variable storage or something?
  // it seems to never be set right after restart, but verifies back fine
  // when actually writing).
  // XXX - Actually, the AT&UPDATE command clears those bytes, so they will
  // always be wrong, but the radio won't boot until they are set again.
  // So we are stuck with this, until we can add commands to our CSMA firmware to
  // allow reading of FLASH memory without entering the bootloader.

  int fail=0;
  
  for(i=0;i<ihex->ihrs_count;i++)
    if (ihex->ihrs_records[i].ihr_type==0x00)
      // if (ihex->ihrs_records[i].ihr_address<0xF7FE)
      {
	if (fail&&(verbose<2)) break;
	
	if (memcmp(&buffer[ihex->ihrs_records[i].ihr_address],
		   ihex->ihrs_records[i].ihr_data,
		   ihex->ihrs_records[i].ihr_length)) {
	  fail=1;
	  if (verbose) {
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
  return fail;
}
