#include "Data.h"
#include "StraightLine.h"
#include "DNest4/code/DNest4.h"

using namespace std;
using namespace DNest4;

StraightLine::StraightLine()
{

}

void StraightLine::calculate_mu()
{
	const auto& x = Data::get_instance().get_x();
	mu = m*x + b;
}

void StraightLine::calculate_mu_proposed()
{
	const auto& x = Data::get_instance().get_x();
	mu_proposed = m_proposed*x + b_proposed;
}

void StraightLine::from_prior(size_t i)
{
    RNG rng(i); 
	// Naive diffuse prior
	m = 1E3*rng.randn();
	b = 1E3*rng.randn();

	// Log-uniform prior
	sigma = exp(-10. + 20.*rng.rand());

	// Compute the model line
	calculate_mu();
}

double StraightLine::perturb(RNG& rng)
{
    m_proposed = m;
    b_proposed = b;
    sigma_proposed = sigma;
	double log_H = 0.;

	// Proposals explore the prior
	// For normal priors I usually use the hastings factor to do it
	int which = rng.rand_int(3);
	if(which == 0)
	{
		log_H -= -0.5*pow(m/1E3, 2);
		m_proposed += 1E3*rng.randh();
		log_H += -0.5*pow(m/1E3, 2);
	}
	else if(which == 1)
	{
		log_H -= -0.5*pow(b/1E3, 2);
		b_proposed += 1E3*rng.randh();
		log_H += -0.5*pow(b/1E3, 2);
	}
	else
	{
		// Usual log-uniform prior trick
		sigma_proposed = log(sigma_proposed);
		sigma_proposed += 20.*rng.randh();
		wrap(sigma_proposed, -10., 10.);
		sigma_proposed = exp(sigma_proposed);
	}

	// Pre-reject
	if(rng.rand() >= exp(log_H))
		return -1E300;
	else
		log_H = 0.0;

	// Calculate mu again if m or b changed
	if(which == 0 || which == 1)
		calculate_mu_proposed();

	return log_H;
}

void StraightLine::accept_perturbation() {
    mu = mu_proposed;
    b = b_proposed;
    m = m_proposed;
    sigma=sigma_proposed;
}

double StraightLine::proposal_log_likelihood() const {
	const auto& y = Data::get_instance().get_y();

	// Variance
	double var = sigma_proposed*sigma_proposed;

	// Conventional gaussian sampling distribution
	return -0.5*y.size()*log(2*M_PI*var) - 0.5*pow(y - mu_proposed, 2).sum()/var;
}

double StraightLine::log_likelihood() const
{
	// Grab the y-values from the dataset
	const auto& y = Data::get_instance().get_y();

	// Variance
	double var = sigma*sigma;

	// Conventional gaussian sampling distribution
	return -0.5*y.size()*log(2*M_PI*var) - 0.5*pow(y - mu, 2).sum()/var;
}

void StraightLine::print(std::ostream& out) const
{
	out<<m<<' '<<b<<' '<<sigma;
}

string StraightLine::description() const
{
	return string("m, b, sigma");
}

