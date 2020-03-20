BIN = bin
SRC = src
OBJ = build
INC = include

TARGET = shfd

CXXFLAGS = -g -Wall -pedantic -pthread -I$(INC)

SRCS = $(wildcard $(SRC)/*.cc)
OBJS = $(patsubst $(SRC)/%.cc, $(OBJ)/%.o, $(SRCS))

$(TARGET): $(OBJS)
	@echo "linking..."
	$(CXX) $(CXXFLAGS) -o $@ $^
	@echo "done!"

$(OBJ)/%.o: $(SRC)/%.cc
	@echo "compiling..."
	$(CXX) $(CXXFLAGS) -o $@ -c $<

.PHONY: clean

clean:
	@echo "cleaning..."
	rm -f $(TARGET) $(OBJS)
