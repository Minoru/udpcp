CXX?=g++
CXXFLAGS?=-std=c++11 -Wall -Wextra -Werror -fsanitize=address -fno-omit-frame-pointer -g -O2

UDPCPD_HOST?=localhost
UDPCPD_PORT?=8765

SHARED_DEPS=src/crc32c.o src/crc32c.h
UDPCP_DEPS=src/udpcp.o src/config.h $(SHARED_DEPS)
UDPCPD_DEPS=src/udpcpd.o src/config.h $(SHARED_DEPS)

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
	./udpcp $(UDPCPD_HOST) $(UDPCPD_PORT) data/16195

run-client2: run-server udpcp
	./udpcp $(UDPCPD_HOST) $(UDPCPD_PORT) data/1

run-client3: run-server udpcp
	./udpcp $(UDPCPD_HOST) $(UDPCPD_PORT) data/19143

run-client4: run-server udpcp
	./udpcp $(UDPCPD_HOST) $(UDPCPD_PORT) data/empty

clean:
	rm -f src/*.o udpcp udpcpd

.PHONY: clean run run-server run-client1 run-client2 run-client3 run-client4
