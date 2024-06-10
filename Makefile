TARGETS = smtp client

all: $(TARGETS)

# Rule to create the smtp executable by linking smtp.o and analyzer.o
smtp: smtp.cc analyzer.o
	g++ -o $@ smtp.cc analyzer.o -lpthread -std=c++11 -g

# Rule to compile analyzer.cc to analyzer.o
analyzer.o: analyzer.cc
	g++ -c -std=c++11 -o $@ $^

# Rule to create the client executable
client: client.cc
	g++ -o $@ client.cc -lpthread -std=c++11 -g

pack:
	rm -f analyzer.zip
	zip -r analyzer.zip *.cc README Makefile

clean::
	rm -fv $(TARGETS) *.o *~

realclean:: clean
	rm -fv analyzer.zip
