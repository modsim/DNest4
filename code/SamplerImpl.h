#include <cstdio>
#include <cassert>
#include <chrono>
#include <iostream>
#include <fstream>
#include <functional>
#include <thread>
#include <algorithm>
#include <iomanip>

#include "Utils.h"
#include "Pybind11_abortable.hpp"

namespace DNest4
{

template<class ModelType>
Sampler<ModelType>::Sampler(unsigned int num_threads, double compression,
							const Options& options, bool save_to_disk,
                            bool _adaptive)
:save_to_disk(save_to_disk)
,thin_print(1)
,threads(num_threads, nullptr)
,barrier(nullptr)
,num_threads(num_threads)
,compression(compression)
,options(options)
,adaptive(_adaptive)
,particles(options.num_particles*num_threads)
,log_likelihoods(options.num_particles*num_threads)
,level_assignments(options.num_particles*num_threads, 0)
,levels(1, LikelihoodType())
,copies_of_levels(num_threads, levels)
,all_above()
,rngs(num_threads)
,count_saves(0)
,count_mcmc_steps_since_save(0)
,count_mcmc_steps(0)
,difficulty(1.0)
,work_ratio(1.0)
,above(num_threads)
{
	assert(num_threads >= 1);
	assert(compression > 1.);

    std::cout << "using threads " << num_threads << std::endl;

    if(options.max_num_levels == 0 && std::abs(compression - exp(1.0)) > 1E-6)
    {
        std::cerr<<"# ERROR: Cannot use -c with max_num_levels=0 (AUTO).";
        std::cerr<<std::endl;
        exit(0);
    }

    // Find best ever particle
    auto indices = argsort(log_likelihoods);
    best_ever_particle = particles[indices.back()];
    best_ever_log_likelihood = log_likelihoods[indices.back()];
    std::cout << std::scientific << std::setprecision(16);
}

template<class ModelType>
void Sampler<ModelType>::save_checkpoint() {
    std::string temp_name = options.checkpoint_file + ".next";
    std::fstream fout(temp_name, std::ios::out);
    if(fout.is_open()) {
        this->print(fout);
        fout.close();
        std::rename(temp_name.c_str(), options.checkpoint_file.c_str());
    }
    else {
        std::cerr << "error saving checkpoint. Continuing" << std::endl;
    }
}

template<class ModelType>
void Sampler<ModelType>::read_checkpoint() {
    std::fstream fin(options.checkpoint_file, std::ios::in);
    if(fin.is_open()) {
        this->read(fin);
    }
    else {
        std::cerr << "error loading checkpoint. Aborting" << std::endl;
        exit(1);
    }
    if(options.max_num_saves != 0 && count_saves>=options.max_num_saves) {
        std::cout << "max num saves already achieved. Increase the max_num_saves to continue sampling." << std::endl;
    }
}


template<class ModelType>
void Sampler<ModelType>::initialise(unsigned int first_seed, bool continue_from_checkpoint)
{
    // Assign memory for storage
    all_above.reserve(2*options.new_level_interval);
    for(auto& a: above) {
        a.reserve(2 * options.new_level_interval);
        a.clear();
    }

    if(continue_from_checkpoint) {
        read_checkpoint();
        std::cout << "# Continuing from checkpoint. ";
        std::cout<< "Loaded " << count_saves << " saves and " << count_mcmc_steps << " mcmc steps." << std::endl;
    }
    else {
        std::cout << "# Seeding random number generators. First seed = ";
        std::cout << first_seed << "." << std::endl;
        // Seed the RNGs, incrementing the seed each time
        for (RNG &rng: rngs) {
            rng.set_seed(first_seed++);
        }

        std::cout << "# Generating " << particles.size();
        std::cout << " particle" << ((particles.size() > 1) ? ("s") : (""));
        std::cout << " from the prior..." << std::flush;
        // Reference to an RNG to use
        RNG& rng = rngs[0];
        for (size_t i = 0; i < particles.size(); ++i) {
            particles[i].from_prior(i);
            log_likelihoods[i] = LikelihoodType(particles[i].log_likelihood(),
                                                rng.rand());
        }
        std::cout << "done." << std::endl;
        initialise_output_files();
    }
}

template<class ModelType>
void Sampler<ModelType>::run(unsigned int thin)
{
	// Set the thining of terminal output
	thin_print = thin;

#ifndef NO_THREADS
	// Set up threads and barrier
	// Delete if necessary (shouldn't be needed!)
	if(barrier != nullptr)
		delete barrier;
	for(auto& thread: threads)
	{
		if(thread != nullptr)
			delete thread;
	}

	isThreadDone = std::vector<bool>(threads.size(), false);
    this->shouldThreadsStop = false;

    // Create the barrier
	barrier = new Barrier(num_threads);

	// Create and launch threads
	for(size_t i=0; i<threads.size(); ++i)
	{
		// Function to run on each thread
		auto func = std::bind(&Sampler<ModelType>::run_thread, this, i);

		// Allocate and create the thread
		threads[i] = new std::thread(func);
	}

    // check whether all threads are done. If they are not, check for signals once every second.
    while (!std::all_of(isThreadDone.begin(), isThreadDone.end(), [](bool v) {return v; })) {
        DNEST4_ABORTABLE;
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }

	// Join and de-allocate all threads and barrier
	for(auto& t: threads) {
        t->join();
    }

	// Delete dynamically allocated stuff and point to nullptr
	delete barrier;
	barrier = nullptr;
	for(auto& t: threads)
	{
		delete t;
		t = nullptr;
	}
#else
	// TODO check signal is caught here too
	for(size_t i=0; i<threads.size(); ++i) run_thread(i);
#endif
}

template<class ModelType>
void Sampler<ModelType>::mcmc_thread(unsigned int thread)
{
	// Reference to the RNG for this thread
	RNG& rng = rngs[thread];

	// Reference to this thread's copy of levels
	std::vector<Level>& _levels = copies_of_levels[thread];

	// First particle belonging to this thread
	const int start_index = thread*options.num_particles;

	// Do some MCMC
	int which;
	for(unsigned int i=0; i<options.thread_steps; ++i) {
		which = start_index + rng.rand_int(options.num_particles);

		if(rng.rand() <= 0.5)
		{
			update_particle(thread, which);
			update_level_assignment(thread, which);
		}
		else
		{
			update_level_assignment(thread, which);
			update_particle(thread, which);
		}
		if(!enough_levels(_levels) && _levels.back().get_log_likelihood() < log_likelihoods[which]) {
            above[thread].push_back(log_likelihoods[which]);
        }
	}
}

template<class ModelType>
void Sampler<ModelType>::update_particle(unsigned int thread, unsigned int which)
{
	// Reference to the RNG for this thread
	RNG& rng = rngs[thread];

	// Reference to copies_of_levels[thread]
	std::vector<Level>& _levels = copies_of_levels[thread];

	// Reference to the level we're in
	Level& level = _levels[level_assignments[which]];

	// Reference to the particle being moved
	ModelType& particle = particles[which];
	LikelihoodType& logl = log_likelihoods[which];

	// Do the proposal for the particle
	double log_H = particle.perturb(rng);

	// Prevent unnecessary exponentiation of a large number
	if(log_H > 0.0)
		log_H = 0.0;

    if(rng.rand() <= exp(log_H))
    {
    	LikelihoodType logl_proposal(particle.proposal_log_likelihood(),
												logl.get_tiebreaker());

        // perturb likelihood to obtain new tiebreaker
        logl_proposal.perturb(rng);

	    // Accept?
	    if(level.get_log_likelihood() < logl_proposal)
	    {
		    particle.accept_perturbation();
		    logl = logl_proposal;
		    level.increment_accepts(1);
	    }
    }

	level.increment_tries(1);

	// Count visits and exceeds
	unsigned int current_level = level_assignments[which];
	for(; current_level < (_levels.size()-1); ++current_level)
	{
		_levels[current_level].increment_visits(1);
		if(_levels[current_level+1].get_log_likelihood() <
											log_likelihoods[which])
			_levels[current_level].increment_exceeds(1);
		else
			break;
	}
}

template<class ModelType>
void Sampler<ModelType>::update_level_assignment(unsigned int thread,
													unsigned int which) {
    // Reference to the RNG for this thread
    RNG &rng = rngs[thread];

    // Reference to this thread's copy of levels
    std::vector<Level> &_levels = copies_of_levels[thread];

    // Generate proposal
    int proposal = static_cast<int>(level_assignments[which])
                   + static_cast<int>(pow(10., 2. * rng.rand()) * rng.randn());

    // If the proposal was to not move, go +- one level
    if (proposal == static_cast<int>(level_assignments[which])) {
        if (rng.rand() < 0.5) {
            proposal = proposal-1;
        } else {
            proposal = proposal+1;
        }
    }

	// Wrap into allowed range
	proposal = DNest4::mod(proposal, static_cast<int>(_levels.size()));

	// Acceptance probability
	double log_A = -_levels[proposal].get_log_X()
					+ _levels[level_assignments[which]].get_log_X();

	// Pushing up part
	log_A += log_push(proposal) - log_push(level_assignments[which]);

	// Enforce uniform exploration part (if all levels exist)
	if(_levels.size() == options.max_num_levels)
		log_A += options.beta*log((double)(_levels[level_assignments[which]].get_tries() + 1)/(double)(_levels[proposal].get_tries() + 1));

	// Prevent exponentiation of huge numbers
	if(log_A > 0.)
		log_A = 0.;

	// Make a LikelihoodType for the proposal
	if(rng.rand() <= exp(log_A) && _levels[proposal].get_log_likelihood() < log_likelihoods[which])
	{
		// Accept
		level_assignments[which] = static_cast<unsigned int>(proposal);
	}
}

template<class ModelType>
void Sampler<ModelType>::run_thread(unsigned int thread)
{
	// Alternate between MCMC and bookkeeping
	while(true)
	{
		// Thread zero takes full responsibility for some tasks
		// Setting up copies of levels
		if(thread == 0)
		{
			// Each thread will write over its own copy of the levels
			for(unsigned int i=0; i<num_threads; ++i) {
                copies_of_levels[i] = levels;
            }
		}

#ifndef NO_THREADS
		// Wait for all threads to get here before proceeding
		barrier->wait();
#endif

		// Check for termination
		if(shouldThreadsStop || (options.max_num_saves != 0 && count_saves != 0 && (count_saves%options.max_num_saves == 0))) {
            isThreadDone[thread] = true;
            return;
		}

		// Do the MCMC (all threads do this!)
		mcmc_thread(thread);

#ifndef NO_THREADS
		barrier->wait();
#endif

		// Thread zero takes full responsibility for some tasks
		if(thread == 0)
		{
			// Count the MCMC steps done
			count_mcmc_steps += num_threads*options.thread_steps;
            count_mcmc_steps_since_save += num_threads*options.thread_steps;

			// Go through copies of levels and apply diffs to levels
			std::vector<Level> levels_orig = levels;
			for(const auto& _levels: copies_of_levels)
			{
				for(size_t i=0; i<levels.size(); ++i)
				{
					levels[i].increment_accepts(_levels[i].get_accepts()
														- levels_orig[i].get_accepts());
					levels[i].increment_tries(_levels[i].get_tries()
														- levels_orig[i].get_tries());
					levels[i].increment_visits(_levels[i].get_visits()
														- levels_orig[i].get_visits());
					levels[i].increment_exceeds(_levels[i].get_exceeds()
														- levels_orig[i].get_exceeds());
				}
			}

			// Combine into a single vector
			for(auto& a: above)
			{
				for(const auto& element: a) {
                    all_above.push_back(element);
                }
				a.clear();
			}

			// Do the bookkeeping
			do_bookkeeping();
		}
	}
}

template<class ModelType>
void Sampler<ModelType>::increase_max_num_saves(unsigned int increment)
{
       unsigned int new_max_num_saves = options.max_num_saves + increment;
       if(new_max_num_saves <= options.max_num_saves) {
           throw std::runtime_error("unsigned integer overflow while trying to increase max_num_saves of DNest4 sampler.");
       }
       options.max_num_saves = new_max_num_saves;
}

template<class ModelType>
bool Sampler<ModelType>::enough_levels(const std::vector<Level>& l) const
{
    if(options.max_num_levels == 0)
    {
        // Check level spacing (in terms of log likelihood)
        // over last n levels
        int num_levels_to_check = static_cast<int>(30*sqrt(0.02*l.size()));
        if(num_levels_to_check < 30)
            return false;

        int k = l.size() - 1;
        double tot = 0.0;
        double max = -1E300;
        for(int i=0; i<num_levels_to_check; ++i)
        {
            double diff = l[k].get_log_likelihood().get_value()
                             - l[k-1].get_log_likelihood().get_value();
            tot += diff;
            if(diff > max)
                max = diff;
            --k;
        }
        if(tot / num_levels_to_check < 0.75 && max < 1.0)
            return true;
        else
            return false;
    }

    // Just compare with the value from OPTIONS
    return (l.size() >= options.max_num_levels);
}

template<class ModelType>
void Sampler<ModelType>::do_bookkeeping()
{
	// Create a new level?
	if(!enough_levels(levels) &&
        (all_above.size() >= options.new_level_interval))
	{
		// Create the level
		std::sort(all_above.begin(), all_above.end());
		int index = static_cast<int>((1. - 1./compression)*all_above.size());
		std::cout<<"# Creating level "<<levels.size()<<" with log likelihood = ";
		std::cout<<all_above[index].get_value()<<"."<<std::endl;

		levels.push_back(Level(all_above[index]));
		all_above.erase(all_above.begin(), all_above.begin() + index + 1);
		for(auto& a:above) {
            a.clear();
        }

		// If last level
		if(enough_levels(levels))
		{
            // Regularisation
            double reg = options.new_level_interval*sqrt(options.lambda);
			Level::renormalise_visits(levels, static_cast<int>(reg));
			all_above.clear();
            std::cout<<"# Done creating levels."<<std::endl;
		}
		else
		{
			// If it's not the last level, look for lagging particles
			kill_lagging_particles();
		}
	}

	// Recalculate log_X values of levels
	Level::recalculate_log_X(levels, compression,
                        options.new_level_interval*sqrt(options.lambda));


    if(!enough_levels(levels) && adaptive)
    {
        // Compute difficulty as a weighted average of compression deviations
        // from the target value
        double gap_norm_tot = 0.0;
        double weight_tot = 0.0;
        for(size_t i=1; i<levels.size(); ++i)
        {
            // Departure of log(X) differences from target value.
            double gap = (levels[i-1].get_log_X() - levels[i].get_log_X())
                            - log(compression);
            double weight = exp(((int)i - (int)levels.size())/3.0);
            gap_norm_tot += weight*std::abs(gap)/log(compression);
            weight_tot += weight;
        }
        difficulty = gap_norm_tot / weight_tot;

        double work_ratio_max = 20.0/sqrt(options.lambda);
        double coeff = (work_ratio_max - 1.0)/(0.1 - 0.02);
        if(difficulty >= 0.1)
            work_ratio = work_ratio_max;
        else if(difficulty >= 0.02)
            work_ratio = 1.0 + coeff*(difficulty - 0.02);
        else
            work_ratio = 1.0;
    }

	if(count_mcmc_steps_since_save >= options.save_interval) {
        ++count_saves;
        count_mcmc_steps_since_save = 0;
        save_levels();
        save_particle();
        save_checkpoint();
        auto indices = argsort(log_likelihoods);
        if (best_ever_log_likelihood < log_likelihoods[indices.back()]) {
            best_ever_particle = particles[indices.back()];
            best_ever_log_likelihood = log_likelihoods[indices.back()];
            save_best_particle();
        }
    }
}

template<class ModelType>
double Sampler<ModelType>::log_push(unsigned int which_level) const
{
	// Reference to this thread's copy of levels
	assert(which_level < levels.size());
	if(enough_levels(levels))
		return 0.0;

	int i = which_level - (static_cast<int>(levels.size()) - 1);
	return static_cast<double>(i)/(work_ratio*options.lambda);
}

template<class ModelType>
void Sampler<ModelType>::initialise_output_files() const
{
	if(!save_to_disk)
		return;

	std::fstream fout;

	// Output headers
	fout.open(options.sample_info_file, std::ios::out);
	fout<<"# level assignment, log likelihood, tiebreaker, ID."<<std::endl;
	fout.close();

	fout.open(options.sample_file, std::ios::out);
	fout<<"# "<<particles[0].description().c_str()<<std::endl;
	fout.close();

	save_levels();
}


template<class ModelType>
void Sampler<ModelType>::save_levels() const
{
	if(!save_to_disk)
		return;

	// Output file
	std::fstream fout;
	fout.open(options.levels_file, std::ios::out);
	fout<<"# log_X, log_likelihood, tiebreaker, accepts, tries, exceeds, visits";
	fout<<std::endl;
    if(options.write_exact_representation) {
        fout << std::hexfloat;
    }
    else {
        fout << std::scientific << std::setprecision(16);
    }


	for(const Level& level: levels)
	{
		fout<<level.get_log_X()<<' ';
		fout<<level.get_log_likelihood().get_value()<<' ';
		fout<<level.get_log_likelihood().get_tiebreaker()<<' ';
		fout<<level.get_accepts()<<' ';
		fout<<level.get_tries()<<' ';
		fout<<level.get_exceeds()<<' ';
		fout<<level.get_visits()<<std::endl;
	}
	fout.close();
}

template<class ModelType>
void Sampler<ModelType>::save_best_particle() const
{
    std::fstream fout;
    fout.open(options.best_particle_file, std::ios::out|std::ios::app);
    if(options.write_exact_representation) {
        fout << std::hexfloat;
    }
    else {
        fout << std::scientific << std::setprecision(16);
    }
    best_ever_particle.print(fout);
    fout<<std::endl;
    fout.close();
    fout.open(options.best_likelihood_file, std::ios::out|std::ios::app);
    // always write best likelihood in readable format
    fout << std::scientific << std::setprecision(16);
    best_ever_log_likelihood.print(fout);
    fout<<std::endl;
    fout.close();
}

template<class ModelType>
void Sampler<ModelType>::save_particle()
{
	if(!save_to_disk)
		return;

	int which = rngs[0].rand_int(particles.size());
    std::fstream fout;
    fout.open(options.sample_file, std::ios::out|std::ios::app);
    if(options.write_exact_representation) {
        fout << std::hexfloat;
    }
    else {
        fout << std::scientific << std::setprecision(16);
    }
	particles[which].print(fout);
	fout<<std::endl;
	fout.close();

	fout.open(options.sample_info_file, std::ios::out|std::ios::app);
    if(options.write_exact_representation) {
        fout << std::hexfloat;
    }
    else {
        fout << std::scientific << std::setprecision(16);
    }

	fout<<level_assignments[which]<<' ';
	fout<<log_likelihoods[which].get_value()<<' ';
	fout<<log_likelihoods[which].get_tiebreaker()<<' ';
	fout<<which<<std::endl;
	fout.close();
}

template<class ModelType>
void Sampler<ModelType>::kill_lagging_particles()
{
	// Count the number of deletions
	static unsigned int deletions = 0;

	// Flag each particle as good or bad
	std::vector<bool> good(num_threads*options.num_particles, true);

	// How good is the best particle?
	double max_log_push = -std::numeric_limits<double>::max();

	unsigned int num_bad = 0;
    double kill_probability = 0.0;
	for(size_t i=0; i<particles.size(); ++i)
	{
		if(log_push(level_assignments[i]) > max_log_push)
			max_log_push = log_push(level_assignments[i]);
        kill_probability = pow(1.0 - 1.0/(1.0 + exp(-log_push(level_assignments[i]) - 4.0)), 3);

		if(rngs[0].rand() <= kill_probability)
		{
			good[i] = false;
			++num_bad;
		}
	}

	if(num_bad < num_threads*options.num_particles)
	{
		// Replace bad particles with copies of good ones
		for(size_t i=0; i<particles.size(); ++i)
		{
			if(!good[i])
			{
				// Choose a replacement particle. Higher prob
				// of selecting better particles.
				int i_copy;
				do
				{
					i_copy = rngs[0].rand_int(num_threads*options.num_particles);
				}while(!good[i_copy] ||
			        rngs[0].rand() >= exp(log_push(level_assignments[i_copy]) - max_log_push));

				particles[i] = particles[i_copy];
				log_likelihoods[i] = log_likelihoods[i_copy];
                std::cout << "writing all above as" << std::endl;
				level_assignments[i] = level_assignments[i_copy];
				++deletions;

				std::cout<<"# Replacing lagging particle.";
				std::cout<<" This has happened "<<deletions;
				std::cout<<" times."<<std::endl;
			}
		}
	}
}

template<class ModelType>
void Sampler<ModelType>::print(std::ostream& out) const
{

    out<<options<<' ';
    out << std::hexfloat;

    out<<count_saves<<' ';
    out<<count_mcmc_steps<<' ';
    out<<count_mcmc_steps_since_save<<' ';
    out<<difficulty<<' ';
    out<<work_ratio<<' ';

    out<<save_to_disk<<' ';
    out<<num_threads<<' ';
    out<<compression<<' ';

    out << particles.size() << ' ';
    for(const auto& p: particles) {
        p.print(out);
        p.print_internal(out);
    }

    out << log_likelihoods.size() << ' ';
    for(const auto& l: log_likelihoods) {
        l.print(out);
    }

    out << level_assignments.size() << ' ';
    for(const auto& l: level_assignments) {
        out << l << ' ';
    }

    out << levels.size() << ' ';
    for(const auto& l: levels) {
        l.print(out);
    }

    out << all_above.size() << ' ';
    for(const auto& l: all_above) {
        l.print(out);
    }

    out << rngs.size() << ' ';
    for (const auto& r : rngs) {
        r.engine.serialize(out);
    }
}

template<class ModelType>
void Sampler<ModelType>::read(std::istream& in)
{
    // We want to log the used options, but we do not want to overwrite
    // the passed options. We need to read the options so the stream skips over them.
    Options throw_away_options;
    in>>throw_away_options;

    in>>count_saves;
    in>>count_mcmc_steps;
    in>>count_mcmc_steps_since_save;
    std::string difficulty_string, work_ratio_string;
    in>>difficulty_string;
    difficulty = std::strtod(difficulty_string.c_str(), NULL);
    in>>work_ratio_string;
    work_ratio = std::strtod(work_ratio_string.c_str(), NULL);

    in>>save_to_disk;
    in>>num_threads;
    // doubles written in hexfloat format to avoid loss of precision
    // correctly parsing hexfloat requires a temporary string and std::strtod
    std::string temp_string;
    in>>temp_string;
    compression = std::strtod(temp_string.c_str(), NULL);

    size_t num_particles;
    in >> num_particles;
    particles.clear();
    for(size_t i=0; i<num_particles;++i) {
        ModelType p;
        p.read(in);
        p.read_internal(in);
        particles.push_back(p);
    }

    size_t num_log_likelihoods;
    in >> num_log_likelihoods;
    log_likelihoods.clear();
    for(size_t i=0; i<num_log_likelihoods;++i) {
        LikelihoodType l;
        l.read(in);
        log_likelihoods.push_back(l);
    }

    size_t num_level_assignments;
    in >> num_level_assignments;
    level_assignments.clear();
    for(size_t i=0; i<num_level_assignments;++i) {
        unsigned int l;
        in>>l;
        level_assignments.push_back(l);
    }

    size_t num_levels;
    in >> num_levels;
    levels.clear();
    for(size_t i=0; i<num_levels;++i) {
        Level level;
        level.read(in);
        levels.push_back(level);
    }

    size_t num_all_above;
    in >> num_all_above;
    all_above.clear();
    for(size_t i=0; i<num_all_above;++i) {
        LikelihoodType l;
        l.read(in);
        all_above.push_back(l);
    }

    size_t num_rngs;
    in >> num_rngs;
    in.get();  // To consume the space after num_rngs
    for (size_t i = 0; i < num_rngs; ++i) {
        rngs[i].engine = hops::RandomNumberGenerator::deserialize(in);
    }
}

} // namespace DNest4

