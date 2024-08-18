#include <iostream>
#include "DNest4/code/DNest4.h"
#include "G.h"

using namespace DNest4;

int main(int argc, char** argv)
{
    std::cout << "starting" << std::endl;
    RNG::randh_is_randh2 = true;
    //start<G>(argc, argv);

    CommandLineOptions options(argc, argv);
    Sampler<G> sampler = setup<G>(options);
    std::fstream fin("sampler_state.txt", std::ios::in);
    sampler.read(fin);
    std::cout << "finished reading" << std::endl;
    sampler.run();
	return 0;
}

