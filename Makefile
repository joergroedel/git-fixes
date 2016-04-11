OBJ_FIXES=git-fixes.o
OBJ_SERIES=git-series.o
CXXFLAGS=-O3 -Wall
TARGET_FIXES=git-fixes
TARGET_SERIES=git-series
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

all: $(TARGET_FIXES) $(TARGET_SERIES)

$(TARGET_FIXES): $(OBJ_FIXES)
	g++ -o $@ $+ $(LIBGIT2) $(LIBS)

$(TARGET_SERIES): $(OBJ_SERIES)
	g++ -o $@ $+ $(LIBGIT2) $(LIBS)

%.o: %.cc $(LIBGIT2)
	g++ -c $(CXXFLAGS) $<

$(STATIC_LIBGIT2):
	git submodule init
	git submodule update
	mkdir -p build
	(cd build;cmake -DBUILD_SHARED_LIBS=OFF -DCMAKE_BUILD_TYPE=Release -DBUILD_CLAR=OFF ../libgit2;cmake --build .)

install: $(TARGET_FIXES) $(TARGET_SERIES)
	install -b -D -m 755 $(TARGET_FIXES) $(INSTALL_DIR)
	install -b -D -m 755 $(TARGET_SERIES) $(INSTALL_DIR)

clean:
	rm -f $(OBJ_FIXES)
	rm -f $(TARGET_FIXES) $(TARGET_SERIES)
	rm -rf build

