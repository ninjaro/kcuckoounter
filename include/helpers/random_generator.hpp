#ifndef KCUCKOOUNTER_HELPERS_RANDOM_GENERATOR_HPP
#define KCUCKOOUNTER_HELPERS_RANDOM_GENERATOR_HPP

#include <algorithm>
#include <random>

class random_generator {
public:
    random_generator();

    int uniform_int(int min, int max);
    double uniform_real(double min, double max);

    template <typename It> void shuffle(It begin, It end) {
        std::shuffle(begin, end, engine);
    }

private:
    std::mt19937 engine;
};

#endif // KCUCKOOUNTER_HELPERS_RANDOM_GENERATOR_HPP
