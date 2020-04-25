SRC = src
OBJ = build
INC = include

TARGET = sshell

CXXFLAGS = -g -Wall -pedantic -I$(INC)

SRCS = $(wildcard $(SRC)/*.cc)
OBJS = $(patsubst $(SRC)/%.cc, $(OBJ)/%.o, $(SRCS))

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(OBJ)/%.o: $(SRC)/%.cc
	$(CXX) $(CXXFLAGS) -o $@ -c $<

.PHONY: clean

clean:
	rm -f $(TARGET) $(OBJS)
