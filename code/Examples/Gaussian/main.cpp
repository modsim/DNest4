#include <iostream>
#include "DNest4/code/DNest4.h"
#include "G.h"

using namespace DNest4;

int main(int argc, char** argv)
{
    RNG::randh_is_randh2 = true;
	start<G>(argc, argv);
	return 0;
}

