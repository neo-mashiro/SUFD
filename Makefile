SRC = src
OBJ = build
INC = include

TARGET = sufd

CXXFLAGS = -g -Wall -pedantic -pthread -w -I$(INC)

SRCS = $(wildcard $(SRC)/*.c)
OBJS = $(patsubst $(SRC)/%.c, $(OBJ)/%.o, $(SRCS))

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^
	@echo "Build success!"

$(OBJ)/%.o: $(SRC)/%.c
	$(CXX) $(CXXFLAGS) -o $@ -c $<

.PHONY: clean

clean:
	rm -f $(TARGET) $(OBJS)
