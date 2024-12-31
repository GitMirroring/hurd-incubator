CC=gcc
CFLAGS=-O0 -g3 -Wall $(shell pkg-config --cflags smbclient)
LDLIBS=  $(shell pkg-config --libs smbclient) -lnetfs -lfshelp -liohelp -lpthread -lports -lihash -ldl -lshouldbeinlibc

smbfs: clean smb.o smbfs.o smbnetfs.o
	$(CC) smb.o smbfs.o smbnetfs.o -osmbfs $(LDLIBS)

smb.o:
	$(CC)  $(CFLAGS) smb.c  -I/local/samba/include/ -c  
        
smbfs.o:
	$(CC)   $(CFLAGS) smbfs.c  -I/local/samba/include/ -c  
        
smbnetfs.o:
	$(CC)   $(CFLAGS) smbnetfs.c  -I/local/samba/include/ -c  
        
clean:
	rm -rf *.o smbfs
        
all: smbfs
