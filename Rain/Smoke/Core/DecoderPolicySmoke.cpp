#include "Fixtures/Models.hpp"
#include "Smoke.hpp"

#include "Rain/Coders/JsonDecoder.hpp"

#include <sstream>

namespace RainSmoke {

bool testMissingFieldPolicies() {
    const char *input = R"json(
{
  "name": "decoded"
}
)json";

    bool ok = true;

    {
        std::stringstream stream(input);
        Fixtures::MissingPolicySample sample;
        Rain::JsonDecoder decoder({Rain::MissingFieldPolicy::KeepCurrentValue});

        ok = expect(decodeFrom(decoder, stream, sample), "missing fields keep policy decode") && ok;
        ok = expect(sample.required == 7, "missing fields keep current int") && ok;
        ok = expect(sample.name == "decoded", "missing fields keep policy decodes present field") &&
             ok;
        ok = expect(
                 sample.maybe.has_value() && sample.maybe.value() == 9,
                 "missing fields keep optional"
             ) &&
             ok;
    }

    {
        std::stringstream stream(input);
        Fixtures::MissingPolicySample sample;
        Rain::JsonDecoder decoder({Rain::MissingFieldPolicy::DefaultConstruct});

        ok = expect(decodeFrom(decoder, stream, sample), "missing fields default policy decode") &&
             ok;
        ok = expect(sample.required == 0, "missing fields default int") && ok;
        ok = expect(sample.name == "decoded", "missing fields default decodes present field") && ok;
        ok = expect(!sample.maybe.has_value(), "missing fields default optional") && ok;
    }

    {
        std::stringstream stream(input);
        Fixtures::MissingPolicySample sample;
        Rain::JsonDecoder decoder({Rain::MissingFieldPolicy::Fail});

        ok = expect(!decodeFrom(decoder, stream, sample), "missing fields fail policy rejects") &&
             ok;
    }

    return ok;
}

} // namespace RainSmoke
