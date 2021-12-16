CXX?=g++
CXXFLAGS?=-std=c++11 -Wall -Wextra -Werror

UDPCPD_HOST?=localhost
UDPCPD_PORT?=8765

UDPCP_DEPS=src/udpcp.o src/config.h
UDPCPD_DEPS=src/udpcpd.o src/config.h

all: udpcp udpcpd

%.o: %.cc Makefile
	$(CXX) $(CXXFLAGS) -c -o $@ -c $<

udpcp: Makefile $(UDPCP_DEPS)
	$(CXX) $(CXXFLAGS) -o $@ $(UDPCP_DEPS)

udpcpd: Makefile $(UDPCPD_DEPS)
	$(CXX) $(CXXFLAGS) -o $@ $(UDPCPD_DEPS)

run: run-server run-client1 run-client2 run-client3 run-client4

run-server: udpcpd
	./udpcpd $(UDPCPD_HOST) $(UDPCPD_PORT) &

run-client1: run-server udpcp
	./udpcp $(UDPCPD_HOST) $(UDPCPD_PORT) udpcp

run-client2: run-server udpcp
	./udpcp $(UDPCPD_HOST) $(UDPCPD_PORT) udpcpd

run-client3: run-server udpcp
	./udpcp $(UDPCPD_HOST) $(UDPCPD_PORT) /dev/null

run-client4: run-server udpcp
	./udpcp $(UDPCPD_HOST) $(UDPCPD_PORT) /proc/sys/kernel/random/uuid

clean:
	rm -f src/*.o udpcp udpcpd

.PHONY: clean run run-server run-client1 run-client2 run-client3 run-client4
