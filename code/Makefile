CXX = g++
export CXX
CXXFLAGS = -std=c++11 -O3 -march=native -Wall -Wextra -pedantic -DNDEBUG -I ../../../../
LDFLAGS= -L/home/jadebeck/installed/hops/lib -lhops

SRCS = $(wildcard *.cpp) \
       $(wildcard Distributions/*.cpp) \
       $(wildcard RJObject/ConditionalPriors/*.cpp)

OBJS = $(patsubst %.cpp,%.o,$(SRCS))

all: examples $(OBJS) ${INC} libdnest4.a
noexamples: $(OBJS) ${INC} libdnest4.a

libdnest4.a: $(OBJS)
	@echo Creating $@ library
	@ar rcs $@ $^

examples: $(OBJS) libdnest4.a
	# make nolib -C Examples/SpikeSlab 
	make nolib -C Examples/StraightLine 
	# make nolib -C Examples/RJObject_1DMixture -I ../../../../
	# make nolib -C Examples/RJObject_SineWaves -I ../../../../
	# make nolib -C Examples/RJObject_GalaxyField -I ../../../../
	# make nolib -C Examples/ABC -I ../../../../
	# make nolib -C Examples/Rosenbrock -I ../../../../
	# make nolib -C Examples/Rosenbrock2 -I ../../../../
	# make nolib -C Examples/LennardJones -I ../../../../
	# make nolib -C Examples/Optimizer -I ../../../../


windows:
	x86_64-w64-mingw32-g++-posix -I. -std=c++11 -O3 -Wall -Wextra -pedantic -DNDEBUG -c $(SRCS)
	x86_64-w64-mingw32-ar rcs dnest4.lib *.o
	rm -f *.o

clean:
	rm -f libdnest4.a
	rm -f *.o Distributions/*.o
