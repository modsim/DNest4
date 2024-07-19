#ifndef DNest4_Rosenbrock
#define DNest4_Rosenbrock

#include "DNest4/code/DNest4.h"
#include <valarray>
#include <ostream>

class G
{
	private:
        double x0;
        double x1;

	public:
		// Constructor only gives size of params
		G();

		// Generate the point from the prior
		void from_prior(DNest4::RNG& rng);

		// Metropolis-Hastings proposals
		double perturb(DNest4::RNG& rng);

		// Likelihood function
		double log_likelihood() const;

		// Print to stream
		void print(std::ostream& out) const;

		// Return string with column information
		std::string description() const;
};

#endif

