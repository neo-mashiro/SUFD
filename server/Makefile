BIN = bin
SRC = src
OBJ = build
INC = include

TARGET = shfd

CXXFLAGS = -g -Wall -pedantic -pthread -w -I$(INC)

SRCS = $(wildcard $(SRC)/*.c)
OBJS = $(patsubst $(SRC)/%.c, $(OBJ)/%.o, $(SRCS))

$(TARGET): $(OBJS)
	@echo "linking..."
	$(CXX) $(CXXFLAGS) -o $@ $^
	@echo "done!"

$(OBJ)/%.o: $(SRC)/%.c
	@echo "compiling..."
	$(CXX) $(CXXFLAGS) -o $@ -c $<

.PHONY: clean

clean:
	@echo "cleaning..."
	rm -f $(TARGET) $(OBJS)
