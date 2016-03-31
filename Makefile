OBJ=git-fixes.o
CXXFLAGS=-O3 -Wall -g -I./libgit2/include/
LDFLAGS=-lgit2
TARGET=git-fixes
INSTALL_DIR ?= "${HOME}/bin/"
LIBS=-lpthread -lssl -lcrypto -lz
LIBGIT2=build/libgit2.a

$(TARGET): $(OBJ)
	g++ -o $@ $+ $(LIBGIT2) $(LIBS)

%.o: %.cc $(LIBGIT2)
	g++ -c $(CXXFLAGS) $<

$(LIBGIT2):
	git submodule init
	git submodule update
	mkdir -p build
	(cd build;cmake -DBUILD_SHARED_LIBS=OFF -DCMAKE_BUILD_TYPE=Release -DBUILD_CLAR=OFF ../libgit2;cmake --build .)

install: $(TARGET)
	install -b -D -m 755 $(TARGET) $(INSTALL_DIR)

clean:
	rm -f $(OBJ)
	rm -f git-fixes
	rm -rf build

