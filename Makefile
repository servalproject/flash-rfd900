COPT=	-g -Wall

all:	flash900

flash900:	main.c ihex_parse.c ihex_copy.c ihex_record.c config.h cintelhex.h
	gcc $(COPT) -o flash900 main.c ihex_parse.c ihex_copy.c ihex_record.c
