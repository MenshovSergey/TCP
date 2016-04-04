all: netsh
netsh:
	g++ -std=c++11 main.cpp helper.h parsers.h run_piped.h -o netsh
clean:
	rm netsh
