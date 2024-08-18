#ifndef DNest4_StraightLine
#define DNest4_StraightLine

#include "DNest4/code/DNest4.h"
#include <valarray>
#include <ostream>

class StraightLine
{
	private:
		// The slope and intercept
		double m, b;
		double m_proposed, b_proposed;

		// Noise sd
		double sigma;
		double sigma_proposed;

		// Model prediction
		std::valarray<double> mu;
		std::valarray<double> mu_proposed;

		// Compute the model line given the current values of m and b
		void calculate_mu();
		void calculate_mu_proposed();

	public:
		// Constructor
		StraightLine();

		// Generate the point from the prior
		void from_prior(size_t i);

		// Metropolis-Hastings proposals
		double perturb(DNest4::RNG& rng);

		void accept_perturbation();

		// Likelihood function
		double log_likelihood() const;

		// Likelihood function
		double proposal_log_likelihood() const;

		void read(std::istream& in);
		// Print to stream
		void print(std::ostream& out) const;

		// Return string with column information
		std::string description() const;
};

#endif

