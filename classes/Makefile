CXX = g++
CXXFLAGS = -g -pedantic -Wall -Werror

all: triv_client iserv ciserv cserv

## Common:
tcp-utils.o: tcp-utils.h tcp-utils.cc
	$(CXX) $(CXXFLAGS) -c -o tcp-utils.o tcp-utils.cc

## Client:
client.o: tcp-utils.h client.cc
	$(CXX) $(CXXFLAGS) -c -o client.o client.cc

triv_client: client.o tcp-utils.o
	$(CXX) $(CXXFLAGS) -o triv_client client.o tcp-utils.o

## Server:
## (a) iterative server
iserv.o: tcp-utils.h iserv.cc
	$(CXX) $(CXXFLAGS) -c -o iserv.o iserv.cc

iserv: iserv.o tcp-utils.o
	$(CXX) $(CXXFLAGS) -o iserv iserv.o tcp-utils.o

## (b) apparent concurrency
ciserv.o: tcp-utils.h ciserv.cc
	$(CXX) $(CXXFLAGS) -c -o ciserv.o ciserv.cc

ciserv: ciserv.o tcp-utils.o
	$(CXX) $(CXXFLAGS) -o ciserv ciserv.o tcp-utils.o

## (c) true concurrency
cserv.o: tcp-utils.h cserv.cc
	$(CXX) $(CXXFLAGS) -c -o cserv.o cserv.cc

cserv: cserv.o tcp-utils.o
	$(CXX) $(CXXFLAGS) -o cserv cserv.o tcp-utils.o

clean:
	rm -f triv_client iserv ciserv cserv *~ *.o *.bak core \#*
