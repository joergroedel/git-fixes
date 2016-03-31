OBJ=git-fixes.o
CXXFLAGS=-O3 -Wall
TARGET=git-fixes
INSTALL_DIR ?= "${HOME}/bin/"
LIBS=
STATIC_LIBGIT2=build/libgit2.a
LIBGIT2=-lgit2

ifeq ($(BUILD_LIBGIT2), 1)
  LIBGIT2=$(STATIC_LIBGIT2)
  CXXFLAGS+=-I./libgit2/include/
  LIBS=-lpthread -lssl -lcrypto -lz
endif

ifeq ($(DEBUG), 1)
  CXXFLAGS+=-g
endif

$(TARGET): $(OBJ)
	g++ -o $@ $+ $(LIBGIT2) $(LIBS)

%.o: %.cc $(LIBGIT2)
	g++ -c $(CXXFLAGS) $<

$(STATIC_LIBGIT2):
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

