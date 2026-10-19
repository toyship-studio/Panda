# Rain

Rain is a small C++ field reflection and serialization library used by Panda.
It serializes data fields, not functions, and is designed for simple engine data:
assets, saves, editor metadata, and script-facing configuration.

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

## Samples

Samples are small, readable demos intended for documentation and video
walkthroughs. They are built into a single `RainSamples` executable:

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
./scripts/rain_run_smoke.sh
```

Use samples for explanation and manual demos. Use smoke tests for fast
regression coverage.
