#pragma once

#include "Rain/Codable.hpp"
#include "Rain/Rain.hpp"
#include "Rain/UUID.hpp"

#include "Smoke.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace RainSmoke::Fixtures {

enum class BodyType { Hatchback, Sedan, Crossover, Suv, Sport };

struct Leaf : public Rain::Codable {
    std::string value;

    Leaf() = default;

    explicit Leaf(std::string value)
        : value(std::move(value)) {}

    RAIN_FIELDS_BEGIN(Leaf)
    RAIN_FIELD(value)
    RAIN_FIELDS_END
};

struct Node : public Rain::Codable {
    std::string label;
    Leaf leaf;

    Node() = default;

    Node(std::string label, Leaf leaf)
        : label(std::move(label))
        , leaf(std::move(leaf)) {}

    RAIN_FIELDS_BEGIN(Node)
    RAIN_FIELD(label)
    RAIN_FIELD(leaf)
    RAIN_FIELDS_END
};

struct Vehicle : public Rain::Codable {
    std::string mark;
    int year = 0;
    BodyType bodyType = BodyType::Hatchback;
    Node metadata;

    Vehicle() = default;

    Vehicle(std::string mark, int year, BodyType bodyType, Node metadata)
        : mark(std::move(mark))
        , year(year)
        , bodyType(bodyType)
        , metadata(std::move(metadata)) {}

    RAIN_FIELDS_BEGIN(Vehicle)
    RAIN_FIELD(mark)
    RAIN_FIELD(year)
    RAIN_FIELD(bodyType)
    RAIN_FIELD(metadata)
    RAIN_FIELDS_END
};

struct NestedPerson : public Rain::Codable {
    std::string name;
    int age = 0;
    bool hungry = false;
    std::vector<int> scores;
    std::vector<std::vector<Vehicle>> cars;
    std::string surname;
    std::filesystem::path home;

    RAIN_FIELDS_BEGIN(NestedPerson)
    RAIN_FIELD(name)
    RAIN_FIELD(age)
    RAIN_FIELD(hungry)
    RAIN_FIELD(scores)
    RAIN_FIELD(cars)
    RAIN_FIELD(surname)
    RAIN_FIELD(home)
    RAIN_FIELDS_END
};

inline bool operator==(const Leaf &left, const Leaf &right) {
    return left.value == right.value;
}

inline bool operator==(const Node &left, const Node &right) {
    return left.label == right.label && left.leaf == right.leaf;
}

inline bool operator==(const Vehicle &left, const Vehicle &right) {
    return left.mark == right.mark && left.year == right.year && left.bodyType == right.bodyType &&
           left.metadata == right.metadata;
}

inline bool operator==(const NestedPerson &left, const NestedPerson &right) {
    return left.name == right.name && left.age == right.age && left.hungry == right.hungry &&
           left.scores == right.scores && left.cars == right.cars &&
           left.surname == right.surname &&
           left.home.generic_string() == right.home.generic_string();
}

inline NestedPerson makeNestedPerson() {
    Leaf leaf{"123123123"};
    Node node{"Hello world!", leaf};
    Vehicle first{"Lamborgini", 2020, BodyType::Sport, node};
    Vehicle second{"Lada", 2014, BodyType::Sedan, node};

    NestedPerson person;
    person.name = "Bogdan";
    person.age = 10;
    person.hungry = false;
    person.scores = {1, 2, 3, 4};
    person.cars = {{first, second}, {first, second}};
    person.surname = "Churban";
    person.home = "Assets/Characters/Bogdan";
    return person;
}

enum class TimeOfDay { Night, Morning, Afternoon, Evening };

inline const char *timeToString(TimeOfDay time) {
    switch (time) {
        case TimeOfDay::Night:
            return "Night";
        case TimeOfDay::Morning:
            return "Morning";
        case TimeOfDay::Afternoon:
            return "Afternoon";
        case TimeOfDay::Evening:
            return "Evening";
    }
    return "Night";
}

inline TimeOfDay stringToTime(const std::string &time) {
    if (time == "Morning") {
        return TimeOfDay::Morning;
    }
    if (time == "Afternoon") {
        return TimeOfDay::Afternoon;
    }
    if (time == "Evening") {
        return TimeOfDay::Evening;
    }
    return TimeOfDay::Night;
}

struct CustomPayload : public Rain::Codable {
    std::string text = "TEST STRING";
    int value = 10;

    CustomPayload() = default;

    CustomPayload(std::string text, int value)
        : text(std::move(text))
        , value(value) {}

    RAIN_FIELDS_BEGIN(CustomPayload)
    RAIN_FIELD(text)
    RAIN_FIELD(value)
    RAIN_FIELDS_END
};

struct CustomMessage : public Rain::Codable {
    std::string message;
    TimeOfDay time = TimeOfDay::Night;
    CustomPayload payload;

    static void encode(const char *key, Rain::Encoder *encoder, const CustomMessage &data) {
        encoder->beginObject(key);
        encoder->encode("message", data.message);
        const char *time = timeToString(data.time);
        encoder->encode("time", time);
        encoder->encode("payload", data.payload);
        encoder->endObject();
    }

    static bool decode(const char *key, Rain::Decoder *decoder, CustomMessage &data) {
        if (!decoder->beginObject(key)) {
            return false;
        }

        std::string time;
        bool ok = decoder->decode("message", data.message);
        ok = decoder->decode("time", time) && ok;
        data.time = stringToTime(time);
        ok = decoder->decode("payload", data.payload) && ok;
        decoder->endObject();
        return ok;
    }
};

struct CustomEnvelope : public Rain::Codable {
    std::string title;
    std::optional<CustomMessage> message;

    RAIN_FIELDS_BEGIN(CustomEnvelope)
    RAIN_FIELD(title)
    RAIN_FIELD(message)
    RAIN_FIELDS_END
};

inline bool operator==(const CustomPayload &left, const CustomPayload &right) {
    return left.text == right.text && left.value == right.value;
}

inline bool operator==(const CustomMessage &left, const CustomMessage &right) {
    return left.message == right.message && left.time == right.time &&
           left.payload == right.payload;
}

inline bool operator==(const CustomEnvelope &left, const CustomEnvelope &right) {
    return left.title == right.title && left.message == right.message;
}

inline CustomEnvelope makeCustomEnvelope() {
    CustomMessage message;
    message.message = "Test test testtt";
    message.time = TimeOfDay::Morning;
    message.payload.text = "!@#$$%(";
    message.payload.value = 123;

    CustomEnvelope envelope;
    envelope.title = "Hello world!";
    envelope.message = message;
    return envelope;
}

struct OptionalText : public Rain::Codable {
    std::string text;

    RAIN_FIELDS_BEGIN(OptionalText)
    RAIN_FIELD(text)
    RAIN_FIELDS_END
};

struct OptionalRecord : public Rain::Codable {
    std::optional<OptionalText> text;
    bool enabled = false;
    std::optional<int> count;

    RAIN_FIELDS_BEGIN(OptionalRecord)
    RAIN_FIELD(text)
    RAIN_FIELD(enabled)
    RAIN_FIELD(count)
    RAIN_FIELDS_END
};

struct OptionalRoot : public Rain::Codable {
    std::vector<OptionalRecord> records;

    RAIN_FIELDS_BEGIN(OptionalRoot)
    RAIN_FIELD(records)
    RAIN_FIELDS_END
};

struct MapValue : public Rain::Codable {
    std::string text;
    int number = 0;

    MapValue() = default;

    MapValue(std::string text, int number)
        : text(std::move(text))
        , number(number) {}

    RAIN_FIELDS_BEGIN(MapValue)
    RAIN_FIELD(text)
    RAIN_FIELD(number)
    RAIN_FIELDS_END
};

struct MapBucket : public Rain::Codable {
    std::string text;
    std::unordered_map<std::string, MapValue> objects;
    float value = 0.0f;

    RAIN_FIELDS_BEGIN(MapBucket)
    RAIN_FIELD(text)
    RAIN_FIELD(objects)
    RAIN_FIELD(value)
    RAIN_FIELDS_END
};

struct MapRoot : public Rain::Codable {
    std::vector<MapBucket> array;
    std::string message;

    RAIN_FIELDS_BEGIN(MapRoot)
    RAIN_FIELD(array)
    RAIN_FIELD(message)
    RAIN_FIELDS_END
};

inline bool operator==(const MapValue &left, const MapValue &right) {
    return left.text == right.text && left.number == right.number;
}

inline bool operator==(const MapBucket &left, const MapBucket &right) {
    return left.text == right.text && left.objects == right.objects &&
           RainSmoke::closeEnough(left.value, right.value);
}

inline bool operator==(const MapRoot &left, const MapRoot &right) {
    return left.array == right.array && left.message == right.message;
}

inline MapRoot makeMapRoot() {
    MapBucket bucket;
    bucket.text = "B TEXT";
    bucket.value = 3.14f;
    bucket.objects["key1"] = {"test1", -1};
    bucket.objects["key2"] = {"test2", 1};
    bucket.objects["key3"] = {"test3", 10};

    MapRoot root;
    root.array = {bucket};
    root.message = "TEST MESSAGE";
    return root;
}

struct MissingPolicySample : public Rain::Codable {
    int required = 7;
    std::string name = "initial";
    std::optional<int> maybe = 9;

    RAIN_FIELDS_BEGIN(MissingPolicySample)
    RAIN_FIELD(required)
    RAIN_FIELD(name)
    RAIN_FIELD(maybe)
    RAIN_FIELDS_END
};

struct Transform : public Rain::Codable {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;

    Transform() = default;

    Transform(float x, float y, float z)
        : x(x)
        , y(y)
        , z(z) {}

    RAIN_FIELDS_BEGIN(Transform)
    RAIN_FIELD(x)
    RAIN_FIELD(y)
    RAIN_FIELD(z)
    RAIN_FIELDS_END
};

struct Item : public Rain::Codable {
    std::string name;
    uint32_t amount = 0;
    std::optional<float> durability;

    Item() = default;

    Item(std::string name, uint32_t amount, std::optional<float> durability)
        : name(std::move(name))
        , amount(amount)
        , durability(durability) {}

    RAIN_FIELDS_BEGIN(Item)
    RAIN_FIELD(name)
    RAIN_FIELD(amount)
    RAIN_FIELD(durability)
    RAIN_FIELDS_END
};

struct Player : public Rain::Codable {
    Rain::UUID id = Rain::UUID(0);
    std::string name;
    int level = 0;
    bool alive = false;
    double score = 0.0;
    Transform transform;
    std::vector<Item> inventory;
    std::unordered_map<std::string, Transform> checkpoints;
    std::optional<std::string> title;

    RAIN_FIELDS_BEGIN(Player)
    RAIN_FIELD(id)
    RAIN_FIELD(name)
    RAIN_FIELD(level)
    RAIN_FIELD(alive)
    RAIN_FIELD(score)
    RAIN_FIELD(transform)
    RAIN_FIELD(inventory)
    RAIN_FIELD(checkpoints)
    RAIN_FIELD(title)
    RAIN_FIELDS_END
};

inline bool operator==(const Transform &left, const Transform &right) {
    return RainSmoke::closeEnough(left.x, right.x) && RainSmoke::closeEnough(left.y, right.y) &&
           RainSmoke::closeEnough(left.z, right.z);
}

inline bool operator==(const Item &left, const Item &right) {
    if (left.name != right.name || left.amount != right.amount ||
        left.durability.has_value() != right.durability.has_value()) {
        return false;
    }
    if (left.durability.has_value() &&
        !RainSmoke::closeEnough(left.durability.value(), right.durability.value())) {
        return false;
    }
    return true;
}

inline bool operator==(const Player &left, const Player &right) {
    return static_cast<uint32_t>(left.id) == static_cast<uint32_t>(right.id) &&
           left.name == right.name && left.level == right.level && left.alive == right.alive &&
           RainSmoke::closeEnough(left.score, right.score) && left.transform == right.transform &&
           left.inventory == right.inventory && left.checkpoints == right.checkpoints &&
           left.title == right.title;
}

inline Player makePlayer() {
    Player player;
    player.id = Rain::UUID(123456789);
    player.name = "Binary coder test";
    player.level = 42;
    player.alive = true;
    player.score = 9876.5;
    player.transform = {10.5f, -2.0f, 4.25f};
    player.inventory = {{"Sword", 1, 0.75f}, {"Potion", 5, {}}, {"Key", 2, 1.0f}};
    player.checkpoints["spawn"] = {0.f, 1.f, 2.f};
    player.checkpoints["boss"] = {100.f, 4.f, -30.f};
    player.title = "Explorer";
    return player;
}

} // namespace RainSmoke::Fixtures
