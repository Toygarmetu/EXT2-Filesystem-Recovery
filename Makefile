# Variables
CXX = g++
CXXFLAGS = -Wall -g
INCLUDES = -I.
SRCS = recext2fs.cpp identifier.cpp
OBJS = recext2fs.o identifier.o
TARGET = recext2fs

# Default target
all: $(TARGET)

# Rule to build the target executable
$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJS)

# Rule to compile C++ source files
recext2fs.o: recext2fs.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c recext2fs.cpp -o recext2fs.o

identifier.o: identifier.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c identifier.cpp -o identifier.o

# Clean rule to remove generated files
clean:
	rm -f $(OBJS) $(TARGET)

# Phony targets
.PHONY: all clean

