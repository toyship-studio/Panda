#pragma once

#include "Rain/Codable.hpp"
#include "Rain/Coder.hpp"
#include "Rain/Rain.hpp"
#include "Rain/UUID.hpp"

#include <cstdint>
#include <optional>
#include <ostream>
#include <istream>
#include <string>
#include <utility>
#include <vector>

namespace RainSamples {

template<typename T>
void encodeTo(Rain::Encoder &encoder, std::ostream &stream, const T &data) {
    encoder.encode(stream, data);
}

template<typename T>
bool decodeFrom(Rain::Decoder &decoder, std::istream &stream, T &data) {
    return decoder.decode(stream, data);
}

struct Vec3 : public Rain::Codable {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    Vec3() = default;

    Vec3(float x, float y, float z)
        : x(x)
        , y(y)
        , z(z) {}

    RAIN_FIELDS_BEGIN(Vec3)
    RAIN_FIELD(x)
    RAIN_FIELD(y)
    RAIN_FIELD(z)
    RAIN_FIELDS_END
};

struct GameItem : public Rain::Codable {
    std::string id;
    uint32_t count = 0;
    std::optional<float> durability;

    GameItem() = default;

    GameItem(std::string id, uint32_t count, std::optional<float> durability)
        : id(std::move(id))
        , count(count)
        , durability(durability) {}

    RAIN_FIELDS_BEGIN(GameItem)
    RAIN_FIELD(id)
    RAIN_FIELD(count)
    RAIN_FIELD(durability)
    RAIN_FIELDS_END
};

struct PlayerSave : public Rain::Codable {
    Rain::UUID id = Rain::UUID(0);
    std::string name;
    int level = 1;
    bool alive = true;
    Vec3 position;
    std::vector<GameItem> inventory;
    std::optional<std::string> title;

    RAIN_FIELDS_BEGIN(PlayerSave)
    RAIN_FIELD(id)
    RAIN_FIELD(name)
    RAIN_FIELD(level)
    RAIN_FIELD(alive)
    RAIN_FIELD(position)
    RAIN_FIELD(inventory)
    RAIN_FIELD(title)
    RAIN_FIELDS_END
};

inline PlayerSave makePlayer() {
    PlayerSave player;
    player.id = Rain::UUID(1001);
    player.name = "Mira";
    player.level = 12;
    player.alive = true;
    player.position = Vec3(10.0f, 2.5f, -7.0f);
    player.inventory = {
        GameItem("torch", 2, 0.75f),
        GameItem("ancient_key", 1, {}),
    };
    player.title = "Cartographer";
    return player;
}

bool sampleBasicJson();
bool sampleYamlAsset();
bool sampleCustomCoder();
bool sampleMissingFields();
bool sampleBinarySave();

} // namespace RainSamples
