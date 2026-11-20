#pragma once
// The JSON files of the editor through Rain: read into a type, written from one — whole or not at
// all (Fern::writeFileAtomically), so that a crash or a full disk never leaves half a file.

#include <Fern/Files/AtomicWrite.hpp>
#include <Panda/Assets/Base/Path.hpp>
#include <Rain/Coders/JsonDecoder.hpp>
#include <Rain/Coders/JsonEncoder.hpp>

#include <fstream>
#include <sstream>
#include <string>

namespace Panda {

// The data of the file; false — there is no file, or it is not what the type reads.
template<typename T>
bool readJsonFile(const path_t &path, T &data) {
    std::ifstream file(path);
    return file.is_open() && Rain::JsonDecoder().decode(file, data);
}

// The data as the file, pretty-printed; false — the file is as it was, and `error`, when given,
// says why.
template<typename T>
bool writeJsonFile(const path_t &path, const T &data, std::string *error = nullptr) {
    std::ostringstream text;
    Rain::JsonEncoder(true).encode(text, data);
    return Fern::writeFileAtomically(path, text.str(), error);
}

} // namespace Panda
