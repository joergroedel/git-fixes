OBJ=git-fixes.o
CXXFLAGS=-O3 -Wall -g -I/usr/local/include
LDFLAGS=-lgit2
TARGET=git-fixes
INSTALL_DIR ?= "${HOME}/bin/"

git-fixes: $(OBJ)
	g++ -g -lgit2 -o $@ $(OBJ)

install: $(TARGET)
	install -b -D -m 755 $(TARGET) $(INSTALL_DIR)

clean:
	rm -f $(OBJ)
	rm -f git-fixes

