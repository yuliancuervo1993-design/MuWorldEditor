#include "WorldMapValidator.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace Silver::WorldEditor;

namespace
{
int passed = 0;
void Check(bool value, const char* name)
{
    if (!value) throw std::runtime_error(name);
    ++passed;
    std::cout << "PASS " << name << '\n';
}

WorldObjectRecord Record(std::int16_t type, float x, float y)
{
    return {type, {x, y, 30.0f}, {0.0f, 0.0f, 0.0f}, 1.0f};
}
}

int main()
{
    try
    {
        WorldObjectFile file;
        file.Records.push_back(Record(1, 100.0f, 100.0f));
        file.Records.push_back(Record(1, 100.0f, 100.0f));
        file.Records.push_back(Record(2, 30000.0f, 100.0f));
        WorldObjectDocument objects;
        objects.Load(std::move(file));

        WorldEffectDocument effects;
        const auto light = effects.AddPointLight({100.0f, 100.0f, 30.0f});
        Check(light != NoEffect, "valid light participates in validation");

        const auto report = WorldMapValidator::Validate(objects, effects);
        Check(report.Passed(), "warnings do not fail check map");
        Check(report.Warnings == 2, "duplicate object and outside-map warning detected");
        Check(report.Issues.size() == 2, "validator emits exact expected issues");

        objects.Set(2, Record(1, 200.0f, 200.0f), "repair duplicate");
        objects.Set(3, Record(2, 300.0f, 300.0f), "repair bounds");
        const auto clean = WorldMapValidator::Validate(objects, effects);
        Check(clean.Passed() && clean.Warnings == 0, "clean map passes validator");
        Check(clean.Issues.size() == 1
            && clean.Issues.front().Severity == WorldValidationSeverity::Info,
            "clean map emits explicit pass issue");

        WorldRegionDocument regions;
        const auto outsideRegion = regions.AddCircle("Outside", 10.0f, 10.0f, 50.0f, "test");
        Check(outsideRegion != NoRegion, "validator fixture creates candidate region");
        WorldTriggerDocument triggers;
        const auto missingRegionTrigger = triggers.Add("Broken", "PlayerEnterRegion", 9999);
        Check(missingRegionTrigger != NoTrigger, "validator fixture creates trigger with unresolved region");

        const auto gameplay = WorldMapValidator::Validate(
            objects, effects, nullptr, nullptr, &regions, &triggers);
        Check(!gameplay.Passed(), "unresolved trigger region fails check map");
        Check(gameplay.Errors == 1, "trigger missing region emits one error");
        Check(gameplay.Warnings == 2, "outside region and actionless trigger emit warnings");
        Check(std::any_of(gameplay.Issues.begin(), gameplay.Issues.end(),
            [outsideRegion](const WorldValidationIssue& issue)
            { return issue.Entity == WorldValidationEntity::Region && issue.Id == outsideRegion; }),
            "region issue is navigable by typed id");
        Check(std::any_of(gameplay.Issues.begin(), gameplay.Issues.end(),
            [missingRegionTrigger](const WorldValidationIssue& issue)
            { return issue.Entity == WorldValidationEntity::Trigger && issue.Id == missingRegionTrigger; }),
            "trigger issue is navigable by typed id");

        std::cout << "WORLD_MAP_VALIDATOR_OK passed=" << passed << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
