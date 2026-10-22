#include "WorldAiCandidateCatalog.h"
#include "WorldAiRequest.h"
#include "json.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace Silver::WorldEditor;
namespace
{
void Check(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

SilverWorldStudioContract LorenciaBridge()
{
    SilverWorldStudioContract bridge;
    bridge.Schema = SilverWorldStudioPlacementSchema;
    bridge.Map = "Lorencia";
    bridge.MapNumber = 0;
    bridge.MapWorld = 1;
    bridge.Placements = {
        {0, 2, 3, std::string("Stone03"), {1000, 2000, 30}, {0, 0, 0}, 1.0f, 50},
        {1, 3, 4, std::string("Tree01"), {2000, 3000, 30}, {0, 0, 0}, 1.0f, 12},
    };
    return bridge;
}

void RequestContract()
{
    WorldObjectRecord record;
    record.Type = 2;
    record.Position = {1000, 2000, 30};
    record.Angle = {0, 15, 90};
    record.Scale = 1.25f;
    WorldObjectDocument document;
    document.Load({ObjectEncoding::MapEncrypted, 0, 1, {record}});
    const auto bridge = LorenciaBridge();
    const auto request = WorldAiRequest::FromSelection(0, 1, document, 1, &bridge,
        WorldAiWorkMode::ImproveExisting, "Mejorar Stone03 conservando identidad MU.");
    const auto json = nlohmann::json::parse(request.ToJson());
    Check(json.at("schema") == SilverWorldEditorAiRequestSchema, "request schema is stable");
    Check(json.at("workMode") == "IMPROVE_EXISTING", "improve mode is explicit");
    Check(json.at("selectedObject") == 1 && json.at("type") == 2, "selection identity is serialized");
    Check(json.at("modelSequence") == 3, "model sequence comes from the bridge type");
    Check(json.at("resolvedBmd") == "Data/Object1/Stone03.bmd", "resolved BMD is logical and safe");
    Check(json.at("expectedPlacements") == 50, "placement budget context comes from bridge");
    Check(json.at("previewOnly") == true && json.at("autoApply") == false, "request cannot auto-apply");

    const auto create = WorldAiRequest::ForPlacement(0, 1, "Lorencia", {500, 600, 20}, &bridge,
        WorldAiWorkMode::CreateNew, "Crear una estatua demoníaca de piedra.");
    const auto createJson = nlohmann::json::parse(create.ToJson());
    Check(createJson.at("workMode") == "CREATE_NEW" && createJson.at("selectedObject").is_null(),
        "create mode is position based");
}

void CandidateCatalog()
{
    const auto root = std::filesystem::path(R"(F:\SCWE-NATIVE\silver-world-editor-ai-catalog-test)");
    std::filesystem::remove_all(root);
    const auto candidate = root / "DemonStatue-20260921";
    std::filesystem::create_directories(candidate / "runtime");
    std::ofstream(candidate / "runtime" / "DemonStatue.bmd") << "BMD12 candidate";
    std::ofstream(candidate / "report.json") << R"({
      "status":"PREPARADO SIN APLICAR", "visualApproval":"PENDIENTE VISUAL",
      "expectedPlacements":50,
      "inspection":{"structuralValidationPassed":true,"nativeLoaderVerified":false,
        "meshes":[{"triangles":3000},{"triangles":12}]}
    })";
    WorldAiCandidateCatalog catalog;
    catalog.Scan(root);
    if (!catalog.Error().empty()) throw std::runtime_error("valid candidate report has no scan error: " + catalog.Error());
    Check(catalog.Assets().size() == 1, "candidate catalog is separate and discoverable");
    const auto& asset = catalog.Assets().front();
    Check(asset.Triangles == 3012 && asset.ExpectedPlacements == 50, "candidate metadata is visible");
    Check(asset.StructuralValidation && !asset.NativeLoaderVerified, "QA states stay distinct");
    std::filesystem::remove_all(root);

    const auto deterministicRoot = root / "deterministic";
    const auto deterministicCandidate = deterministicRoot / "candidate";
    std::filesystem::create_directories(deterministicCandidate / "runtime");
    std::ofstream(deterministicCandidate / "runtime" / "z-last.bmd") << "BMD12 candidate";
    std::ofstream(deterministicCandidate / "runtime" / "a-first.bmd") << "BMD12 candidate";
    std::ofstream(deterministicCandidate / "report.json")
        << R"({"status":"PREPARADO SIN APLICAR"})";
    catalog.Scan(deterministicRoot);
    Check(catalog.Assets().size() == 1, "candidate with multiple runtime models remains inspectable");
    Check(catalog.Assets().front().Bmd.filename() == "a-first.bmd",
        "candidate preview chooses the first runtime model deterministically");
    std::filesystem::remove_all(root);

    const auto fileRoot = root / "not-a-directory";
    std::filesystem::create_directories(root);
    std::ofstream(fileRoot) << "not a candidate catalog";
    catalog.Scan(fileRoot);
    Check(catalog.Assets().empty(), "regular file is not treated as a candidate catalog");
    Check(catalog.Error().empty(), "non-directory catalog root is ignored without a filesystem error");
    std::filesystem::remove_all(root);

    const auto pendingRoot = root / "pending";
    const auto pendingCandidate = pendingRoot / "awaiting-runtime";
    std::filesystem::create_directories(pendingCandidate);
    std::ofstream(pendingCandidate / "report.json") << R"({"status":"GENERATED"})";
    catalog.Scan(pendingRoot);
    Check(catalog.Assets().empty(), "candidate without runtime assets is not offered for placement");
    Check(catalog.Error().empty(), "candidate awaiting runtime conversion is not reported as a catalog error");

    const auto readyAlongsidePending = pendingRoot / "ready-candidate";
    std::filesystem::create_directories(readyAlongsidePending / "runtime");
    std::ofstream(readyAlongsidePending / "runtime" / "Ready.bmd") << "BMD12 candidate";
    std::ofstream(readyAlongsidePending / "report.json")
        << R"({"status":"PREPARADO SIN APLICAR","visualApproval":"PENDIENTE VISUAL"})";
    catalog.Scan(pendingRoot);
    Check(catalog.Assets().size() == 1 && catalog.Assets().front().Name == "ready-candidate",
        "ready candidate remains available alongside an unfinished conversion");
    Check(catalog.Error().empty(), "unfinished conversion does not contaminate a mixed candidate catalog");

    const auto secondReadyCandidate = pendingRoot / "second-ready-candidate";
    std::filesystem::create_directories(secondReadyCandidate / "runtime");
    std::ofstream(secondReadyCandidate / "runtime" / "Second.bmd") << "BMD12 candidate";
    std::ofstream(secondReadyCandidate / "report.json")
        << R"({"status":"PREPARADO SIN APLICAR","visualApproval":"PENDIENTE VISUAL"})";
    catalog.Scan(pendingRoot);
    Check(catalog.Assets().size() == 2,
        "all ready candidates remain visible alongside an unfinished conversion");
    std::filesystem::remove_all(root);

    const auto linkedRoot = root / "linked";
    const auto linkedCandidate = linkedRoot / "external-runtime";
    const auto externalRuntime = root / "external";
    std::filesystem::create_directories(linkedCandidate);
    std::filesystem::create_directories(externalRuntime);
    std::ofstream(linkedCandidate / "report.json") << R"({"status":"GENERATED"})";
    std::ofstream(externalRuntime / "escaped.bmd") << "not a real model";
    std::error_code linkError;
    std::filesystem::create_directory_symlink(externalRuntime, linkedCandidate / "runtime", linkError);
    if (!linkError)
    {
        catalog.Scan(linkedRoot);
        Check(catalog.Assets().empty(), "candidate runtime link is not followed outside its directory");
    }
    std::filesystem::remove_all(root);

    const auto oversizedRoot = root / "oversized";
    for (int index = 0; index < 257; ++index)
        std::filesystem::create_directories(oversizedRoot / ("candidate-" + std::to_string(index)));
    const auto firstBoundedCandidate = oversizedRoot / "candidate-0";
    std::filesystem::create_directories(firstBoundedCandidate / "runtime");
    std::ofstream(firstBoundedCandidate / "runtime" / "Bounded.bmd") << "BMD12 candidate";
    std::ofstream(firstBoundedCandidate / "report.json") << R"({"status":"PREPARADO SIN APLICAR"})";
    catalog.Scan(oversizedRoot);
    Check(catalog.Assets().size() == 1 && catalog.Assets().front().Name == "candidate-0",
        "oversized catalog scans a deterministic bounded prefix");
    Check(catalog.Error().find("256") != std::string::npos,
        "oversized catalog reports its bounded scan instead of terminating the editor");
    std::filesystem::remove_all(root);

    const auto invalidMetadataRoot = root / "invalid-metadata";
    const auto invalidMetadataCandidate = invalidMetadataRoot / "invalid-placement-budget";
    std::filesystem::create_directories(invalidMetadataCandidate / "runtime");
    std::ofstream(invalidMetadataCandidate / "runtime" / "Candidate.bmd") << "BMD12 candidate";
    std::ofstream(invalidMetadataCandidate / "report.json") << R"({"expectedPlacements":-1})";
    catalog.Scan(invalidMetadataRoot);
    Check(catalog.Assets().size() == 1, "candidate with invalid optional metadata remains inspectable");
    Check(catalog.Assets().front().ExpectedPlacements == -1,
        "negative placement budget remains explicitly unavailable");

    std::ofstream(invalidMetadataCandidate / "report.json") << R"({"expectedPlacements":4294967296})";
    catalog.Scan(invalidMetadataRoot);
    Check(catalog.Assets().size() == 1, "candidate with oversized metadata remains inspectable");
    Check(catalog.Assets().front().ExpectedPlacements == -1,
        "oversized placement budget remains explicitly unavailable");

    std::ofstream(invalidMetadataCandidate / "report.json") << R"({"expectedPlacements":2147483647})";
    catalog.Scan(invalidMetadataRoot);
    Check(catalog.Assets().size() == 1, "candidate with maximum placement budget remains inspectable");
    Check(catalog.Assets().front().ExpectedPlacements == 2147483647,
        "maximum signed placement budget remains available without truncation");

    std::ofstream(invalidMetadataCandidate / "report.json") << R"({"expectedPlacements":1.5})";
    catalog.Scan(invalidMetadataRoot);
    Check(catalog.Assets().size() == 1, "candidate with fractional optional metadata remains inspectable");
    Check(catalog.Assets().front().ExpectedPlacements == -1,
        "fractional placement budget remains explicitly unavailable");

    std::ofstream(invalidMetadataCandidate / "report.json") << R"({"expectedPlacements":"50"})";
    catalog.Scan(invalidMetadataRoot);
    Check(catalog.Assets().size() == 1, "candidate with string optional metadata remains inspectable");
    Check(catalog.Assets().front().ExpectedPlacements == -1,
        "string placement budget is not coerced into a numeric value");

    std::ofstream(invalidMetadataCandidate / "report.json") << R"({"expectedPlacements":true})";
    catalog.Scan(invalidMetadataRoot);
    Check(catalog.Assets().size() == 1, "candidate with boolean optional metadata remains inspectable");
    Check(catalog.Assets().front().ExpectedPlacements == -1,
        "boolean placement budget is not coerced into a numeric value");

    std::ofstream(invalidMetadataCandidate / "report.json") << R"({"expectedPlacements":null})";
    catalog.Scan(invalidMetadataRoot);
    Check(catalog.Assets().size() == 1, "candidate with null optional metadata remains inspectable");
    Check(catalog.Assets().front().ExpectedPlacements == -1,
        "null placement budget remains explicitly unavailable");

    std::ofstream(invalidMetadataCandidate / "report.json")
        << R"({"inspection":{"structuralValidationPassed":"yes","nativeLoaderVerified":1}})";
    catalog.Scan(invalidMetadataRoot);
    Check(catalog.Assets().size() == 1,
        "candidate with invalid optional QA metadata remains inspectable");
    Check(!catalog.Assets().front().StructuralValidation && !catalog.Assets().front().NativeLoaderVerified,
        "invalid optional QA metadata keeps conservative validation defaults");

    std::ofstream(invalidMetadataCandidate / "report.json")
        << R"({"inspection":{"structuralValidationPassed":true,"nativeLoaderVerified":true}})";
    catalog.Scan(invalidMetadataRoot);
    Check(catalog.Assets().size() == 1,
        "candidate with boolean QA metadata remains inspectable");
    Check(catalog.Assets().front().StructuralValidation && catalog.Assets().front().NativeLoaderVerified,
        "boolean QA metadata preserves successful validation evidence");

    std::ofstream(invalidMetadataCandidate / "report.json")
        << R"({"inspection":{"structuralValidationPassed":true,"nativeLoaderVerified":"yes"}})";
    catalog.Scan(invalidMetadataRoot);
    Check(catalog.Assets().size() == 1,
        "candidate with mixed QA metadata remains inspectable");
    Check(catalog.Assets().front().StructuralValidation && !catalog.Assets().front().NativeLoaderVerified,
        "valid QA evidence remains independent from an invalid sibling field");

    std::ofstream(invalidMetadataCandidate / "report.json")
        << R"({"inspection":{"meshes":[{"triangles":18446744073709551615},{"triangles":1}]}})";
    catalog.Scan(invalidMetadataRoot);
    Check(catalog.Assets().size() == 1, "candidate with excessive triangle metadata remains inspectable");
    Check(catalog.Assets().front().Triangles == std::numeric_limits<std::size_t>::max(),
        "triangle metadata saturates instead of wrapping to a misleading value");
    std::filesystem::remove_all(root);

    const auto realRoot = WorldAiCandidateCatalog::DefaultRoot();
    if (std::filesystem::is_directory(realRoot))
    {
        WorldAiCandidateCatalog real;
        real.Scan(realRoot);
        if (!real.Error().empty()) throw std::runtime_error("real candidate catalog warning: " + real.Error());
        Check(!real.Assets().empty(), "real Silver AI candidates are discoverable read-only");
        std::cout << "REAL_AI_CANDIDATES " << real.Assets().size() << '\n';
    }
}
}

int main()
{
    try
    {
        RequestContract();
        CandidateCatalog();
        std::cout << "SILVER_AI_CONTRACT_OK requests=1 candidates=1\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "SILVER_AI_CONTRACT_FAIL " << error.what() << '\n';
        return 1;
    }
}
