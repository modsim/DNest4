CXXFLAGS = -std=c++11 -g -march=native -Wall -Wextra -pedantic -DNDEBUG
LIBS = -ldnest4 -lpthread -L/home/jadebeck/installed/hops/lib -lhops


default:
	make noexamples -C ../..
	$(CXX) -I ../../../.. -I ../../../../../../ ${INC} $(CXXFLAGS) -c *.cpp
	$(CXX) -pthread -L ../.. -o main *.o $(LIBS)
	rm *.o

nolib:
	$(CXX) -I ../../../.. -I ../../../../../ $(CXXFLAGS) -c *.cpp
	$(CXX) -pthread -L ../.. -o main *.o $(LIBS)
	rm *.o

