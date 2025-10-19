CXX = g++
CXXFLAGS = -std=c++17 -O3 -Wall -Wextra -pedantic -march=native
LDFLAGS = 

# Source files
SOURCES = main.cpp order_book.cpp
OBJECTS = $(SOURCES:.cpp=.o)
TARGET = order_book_test

# Default target
all: $(TARGET)

# Build the main executable
$(TARGET): $(OBJECTS)
	$(CXX) $(OBJECTS) -o $(TARGET) $(LDFLAGS)

# Build object files
%.o: %.cpp order_book.hpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Debug build
debug: CXXFLAGS = -std=c++17 -g -Wall -Wextra -pedantic -DDEBUG
debug: $(TARGET)

# Clean build artifacts
clean:
	rm -f $(OBJECTS) $(TARGET)

# Run the program
run: $(TARGET)
	./$(TARGET)

# Run with performance profiling
profile: CXXFLAGS += -pg
profile: $(TARGET)
	./$(TARGET)
	gprof $(TARGET) gmon.out > profile.txt

# Check for memory leaks (requires valgrind)
memcheck: debug
	valgrind --leak-check=full --show-leak-kinds=all ./$(TARGET)

# Format code (requires clang-format)
format:
	clang-format -i *.cpp *.hpp

# Static analysis (requires cppcheck)
analyze:
	cppcheck --enable=all --std=c++17 *.cpp *.hpp

.PHONY: all debug clean run profile memcheck format analyze
