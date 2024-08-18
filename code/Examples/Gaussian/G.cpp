#include <iostream>
#include "G.h"
#include "DNest4/code/DNest4.h"

using namespace std;
using namespace DNest4;

double scale = 10;

G::G() {
    x0=-1991.0;
    x1=-1991.0;

}

void G::from_prior(RNG& rng)
{
    x0=-scale+scale*rng.rand();
    x1=-scale+scale*rng.rand();
    // std::cout << "x0" << x0 << ", x1" << x1 << std::endl;
}

double G::perturb(RNG& rng)
{
    for(int i=0; i<1; ++i) {
        x0 += scale*rng.randh();
        x1 += scale*rng.randh();
        wrap(x0, -scale, scale);
        wrap(x1, -scale, scale);
    }
    // std::cout << "sampled " << x0 << ", " << x1 << std::endl;

	return 0.0;
}

double G::log_likelihood() const
{
    double var = 1;
    double mu = 0;
    return -0.5*log(2*M_PI*var) - 0.5*(pow(x0-mu, 2) + pow(x1-mu,2))/var;
}

void G::print(std::ostream& out) const
{
		out<<x0 << ' ' << x1 << ' ';
}

void G::read(std::istream& in) 
{
    std::cout << "reading " << std::endl;
    double _x0;
    double _x1;
    in>>_x0;
    in>>_x1;
    std::cout << "read " << _x0 << " and " << _x1 << std::endl;;
    x0=_x0;
    x1=_x1;
}

string G::description() const
{
	return string("x0, x1");
}

