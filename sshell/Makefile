BIN = bin
SRC = src
OBJ = build
INC = include

TARGET = sshell

CXXFLAGS = -g -Wall -pedantic -I$(INC)

SRCS = $(wildcard $(SRC)/*.cpp)
OBJS = $(patsubst $(SRC)/%.cpp, $(OBJ)/%.o, $(SRCS))

$(BIN)/$(TARGET): $(OBJS)
	@echo "linking..."
	$(CXX) $(CXXFLAGS) -o $@ $^
	@echo "done!"

$(OBJ)/%.o: $(SRC)/%.cpp
	@echo "compiling..."
	$(CXX) $(CXXFLAGS) -o $@ -c $<

.PHONY: clean

clean:
	@echo "cleaning..."
	rm -f $(BIN)/$(TARGET) $(OBJS)
