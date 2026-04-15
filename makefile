# Compiler and Flags
CXX = g++
CXXFLAGS = -Wall -std=c++14 -O2 -march=native
LDFLAGS = -lfftw3f -lraw -lm

# Project Files
SRCS = main.cpp FFT.cpp bmp.cpp scan.cpp XMP_tools.cpp
OBJS = $(SRCS:.cpp=.o)
EXEC = focus_checker

# Default build (standard output, no benchmark timing)
all: $(EXEC)

# Debug build (includes benchmark timing and RGB vs Gray comparison)
debug: CXXFLAGS += -DDEBUG_BENCHMARK
debug: $(EXEC)

$(EXEC): $(OBJS)
	$(CXX) $(OBJS) -o $@ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Cleanup generated files
clean:
	rm -f $(OBJS) $(EXEC) BlurryList.txt