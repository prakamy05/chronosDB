CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -pthread

SRCS = src/storage/disk_manager.cpp \
       src/storage/buffer_pool_manager.cpp \
       src/concurrency/lock_manager.cpp \
       main.cpp

OBJS = $(SRCS:.cpp=.o)
TARGET = chronosdb

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET) chronosdb.db chronosdb.log