all:
	$(CC) smailgun.c -g -o smailgun -lcurl -I /usr/local/include -L /usr/local/lib

clean:
	$(RM) smailgun
