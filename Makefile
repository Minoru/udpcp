CXX?=g++
CXXFLAGS?=-std=c++11 -Wall -Wextra -Werror

UDPCP_DEPS=src/udpcp.o
UDPCPD_DEPS=src/udpcpd.o

all: udpcp udpcpd

%.o: %.cc Makefile
	$(CXX) $(CXXFLAGS) -c -o $@ -c $<

udpcp: Makefile $(UDPCP_DEPS)
	$(CXX) $(CXXFLAGS) -o $@ $(UDPCP_DEPS)

udpcpd: Makefile $(UDPCPD_DEPS)
	$(CXX) $(CXXFLAGS) -o $@ $(UDPCPD_DEPS)

clean:
	rm -f $(UDPCP_DEPS) $(UDPCPD_DEPS)

.PHONY: clean
