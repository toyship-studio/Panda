#include "Samples.hpp"

#include <iostream>

namespace {

struct SampleCase {
    bool (*run)();
};

} // namespace

int main() {
    const SampleCase samples[] = {
        {RainSamples::sampleBasicJson},
        {RainSamples::sampleYamlAsset},
        {RainSamples::sampleCustomCoder},
        {RainSamples::sampleMissingFields},
        {RainSamples::sampleBinarySave},
    };

    bool ok = true;
    for (const SampleCase &sample : samples) {
        ok = sample.run() && ok;
    }

    if (!ok) {
        std::cerr << "\nRain samples failed\n";
        return 1;
    }

    std::cout << "\nRain samples complete\n";
    return 0;
}
