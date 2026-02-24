#include "arch/random_generator.hpp"

random_generator::random_generator()
    : engine(std::random_device {}()) { }

int random_generator::uniform_int(int min, int max) {
    std::uniform_int_distribution<int> dist(min, max);
    return dist(engine);
}

double random_generator::uniform_real(double min, double max) {
    std::uniform_real_distribution<double> dist(min, max);
    return dist(engine);
}
