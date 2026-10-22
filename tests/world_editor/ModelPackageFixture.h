#pragma once
#include <bit>
#include <cstdint>
#include <string>
#include <vector>

namespace Silver::WorldEditor::Tests
{
// One triangle, one root bone, first pose; actual native BMD v10 record layout.
inline std::vector<std::uint8_t> PackageModelFixture()
{
    std::vector<std::uint8_t> bytes{'B','M','D',10};
    const auto number = [&](std::uint32_t value, int count) {
        for (int i=0; i<count; ++i) bytes.push_back(static_cast<std::uint8_t>(value >> (i*8)));
    };
    const auto word = [&](int value) { number(static_cast<std::uint16_t>(value), 2); };
    const auto scalar = [&](float value) { number(std::bit_cast<std::uint32_t>(value), 4); };
    const auto point = [&](float x,float y,float z) { scalar(x);scalar(y);scalar(z); };
    const auto name = [&](const std::string& value) {
        const auto start=bytes.size(); bytes.insert(bytes.end(),value.begin(),value.end()); bytes.resize(start+32);
    };
    name("tree.smd"); word(1);word(1);word(1);
    word(3);word(3);word(3);word(1);word(0);
    for(int i=0;i<3;++i){word(0);word(0);point(i==1?1.0f:0.0f,0,i==2?1.0f:0.0f);}
    for(int i=0;i<3;++i){word(0);word(0);point(0,1,0);word(i);word(0);}
    for(int i=0;i<3;++i){scalar(i==1?1.0f:0.0f);scalar(i==2?1.0f:0.0f);}
    const auto face=bytes.size();bytes.push_back(3);bytes.push_back(0);
    for(int i=0;i<3;++i){word(0);word(1);word(2);word(0);}
    bytes.resize(face+64);name("tree.tga");word(1);bytes.push_back(0);
    bytes.push_back(0);name("root");word(-1);point(0,0,0);point(0,0,0);
    return bytes;
}
}
