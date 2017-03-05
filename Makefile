COPT=	-g -Wall

all:	flash900

ISO_3166-1.xml:
	wget -q -O ISO_3166-1.xml https://en.wikipedia.org/wiki/Special:Export/ISO_3166-1

countries.h:	ISO_3166-1.xml parsecountries
	./parsecountries ISO_3166-1.xml countries.h

parsecountries:	Makefile parsecountries.c
	$(CC) $(COPT) -o parsecountries parsecountries.c

flash900:	main.c ihex_parse.c ihex_copy.c ihex_record.c speed_detect.c config.h cintelhex.h sha3.c sha3.h eeprom.c flash900.h miniz.c regulatory.c countries.h Makefile
	$(CC) $(COPT) -o flash900 main.c ihex_parse.c ihex_copy.c ihex_record.c speed_detect.c sha3.c eeprom.c regulatory.c $(LOPT)
