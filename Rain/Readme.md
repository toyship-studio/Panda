# Rain

Rain is a small C++ field reflection and serialization library used by Panda.
It serializes data fields, not functions, and is designed for simple engine data:
assets, saves, editor metadata, and script-facing configuration.

Rain lives in the [Panda](https://github.com/toyship-studio/Panda) engine repository;
[toyship-studio/Rain](https://github.com/toyship-studio/Rain) is its read-only mirror. Issues and
pull requests go to the engine repository.

## Formats

Rain currently supports:

- JSON via `Rain::JsonEncoder` / `Rain::JsonDecoder`.
- YAML via `Rain::YamlEncoder` / `Rain::YamlDecoder`.
- Binary via `Rain::BinaryEncoder` / `Rain::BinaryDecoder`.

## Declaring Serializable Types

Types inherit from `Rain::Codable` and expose fields with the `RAIN_FIELDS_*`
macros:

```cpp
struct PlayerSave : public Rain::Codable {
    std::string name;
    int level = 1;

    RAIN_FIELDS_BEGIN(PlayerSave)
    RAIN_FIELD(name)
    RAIN_FIELD(level)
    RAIN_FIELDS_END
};
```

Rain supports primitive values, strings, paths, UUIDs, enums, optionals,
vectors, `std::unordered_map<std::string, T>`, nested `Codable` objects, and
custom encode/decode hooks.

## Building

Rain needs CMake 3.22 or newer and a C++20 compiler. It carries its dependencies, RapidJSON and
yaml-cpp, in `Vendor/`, so the folder builds on its own; the samples and the smoke test come
with it:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/Samples/RainSamples
```

In another CMake project, add the folder or fetch the mirror and link the `Rain` target:

```cmake
include(FetchContent)
FetchContent_Declare(Rain GIT_REPOSITORY https://github.com/toyship-studio/Rain.git GIT_TAG main)
FetchContent_MakeAvailable(Rain)
target_link_libraries(MyApp PRIVATE Rain)
```

## Samples

Samples are small, readable demos intended for documentation and video
walkthroughs. They are built into a single `RainSamples` executable. Inside the
Panda engine tree:

```sh
cmake --build cmake-build-debug --target RainSamples --parallel 8
./cmake-build-debug/Rain/Samples/RainSamples
```

Sample files:

- [BasicJsonSample.cpp](Samples/BasicJsonSample.cpp): serialize a player save to JSON and read it back.
- [YamlAssetSample.cpp](Samples/YamlAssetSample.cpp): load and emit an asset-like YAML document.
- [CustomCoderSample.cpp](Samples/CustomCoderSample.cpp): map an enum to a readable string with custom encode/decode hooks.
- [MissingFieldsSample.cpp](Samples/MissingFieldsSample.cpp): keep current values when loading old data that misses newer fields.
- [BinarySaveSample.cpp](Samples/BinarySaveSample.cpp): write and read a compact binary save.

## Smoke Tests

Smoke tests are automated checks, separate from samples. They are built into
one `RainSmoke` executable and should return a non-zero exit code on failure:

```sh
./scripts/rain_run_smoke.sh     # in the Panda engine tree
./build/Smoke/RainSmoke         # in a build of this folder alone
```

Use samples for explanation and manual demos. Use smoke tests for fast
regression coverage.

## License

Rain is licensed under the MIT License: [LICENSE.txt](LICENSE.txt). RapidJSON and yaml-cpp in
`Vendor/` come with their own licenses, both MIT.
