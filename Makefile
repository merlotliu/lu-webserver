SRC=$(wildcard ./src/*.cpp)
INCLUDE=./include/
OBJS=$(patsubst %.cpp,%.o,$(SRC))
TARGET=app

# link lib
$(TARGET):$(OBJS)
	$(CXX) $(OBJS) -o $(TARGET) -pthread 

# generate .o file
%.o:%.cpp
	$(CXX) -std=c++11 -g -c $< -o $@ -I $(INCLUDE)

clean:
	rm -rf $(OBJS)