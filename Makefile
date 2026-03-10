CXX := g++
CXXFLAGS := -std=c++20 -Wall -Wextra -pedantic -Isrc

SRC := \
	main.cpp \
	src/db/Database.cpp \
	src/storage/Pager.cpp \
	src/tree/BPlusTree.cpp \
	src/util/DebugPrinter.cpp \
	src/util/InvariantChecker.cpp

TEST_SRC := \
	tests/database_tests.cpp \
	src/db/Database.cpp \
	src/storage/Pager.cpp \
	src/tree/BPlusTree.cpp \
	src/util/DebugPrinter.cpp \
	src/util/InvariantChecker.cpp

.PHONY: all run test clean

all: simple_db

simple_db: $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) -o $@

run: simple_db
	./simple_db

simple_db_tests: $(TEST_SRC)
	$(CXX) $(CXXFLAGS) $(TEST_SRC) -o $@

test: simple_db_tests
	./simple_db_tests

clean:
	rm -f simple_db simple_db_tests
