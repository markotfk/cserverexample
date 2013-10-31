SERVERPORT=1200

all:
	$(MAKE) -C server
	$(MAKE) -C client

runserver:
	./server/server $(SERVERPORT)

runclient:
	./client/client localhost $(SERVERPORT)
