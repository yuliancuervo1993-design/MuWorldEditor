#include "WorldModelPreview.h"
#include "WorldModelInspection.h"
#include "WorldObjectCodec.h"
#include "WorldFileIo.h"
#include "WorldNpcVisualBindings.h"
#include <cmath>
#include <set>
#include <algorithm>
#include <bit>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
using namespace Silver::WorldEditor;
namespace
{
int checks = 0;
void Check(bool value, const std::string& what) { ++checks; if (!value) throw std::runtime_error(what); }
template<class F> void Reject(F action, const char* what)
{ bool rejected = false; try { action(); } catch (const std::exception&) { rejected = true; } Check(rejected, what); }
using Bytes = std::vector<std::uint8_t>;
void Number(Bytes& bytes, std::uint32_t value, std::size_t count)
{ for (std::size_t i = 0; i < count; ++i) bytes.push_back(static_cast<std::uint8_t>(value >> (i * 8))); }
void Short(Bytes& bytes, int value) { Number(bytes, static_cast<std::uint16_t>(value), 2); }
void Float(Bytes& bytes, float value) { Number(bytes, std::bit_cast<std::uint32_t>(value), 4); }
void Point(Bytes& bytes, float x, float y, float z) { Float(bytes, x); Float(bytes, y); Float(bytes, z); }
void Name(Bytes& bytes, const std::string& name)
{ Check(name.size() < 32, "test fixture name bounded"); bytes.insert(bytes.end(), name.begin(), name.end()); bytes.resize(bytes.size() + 32 - name.size()); }
Bytes Fixture(const std::string& texture = "bark.jpg", int faces = 1)
{
    Bytes b{'B','M','D',10}; Name(b, "tree.smd"); Short(b,1); Short(b,1); Short(b,1);
    Short(b,3); Short(b,3); Short(b,3); Short(b,faces); Short(b,0);
    for(int i=0;i<3;++i) { Short(b,0); Short(b,0); Point(b,static_cast<float>(i==1),0,static_cast<float>(i==2)); }
    for(int i=0;i<3;++i) { Short(b,0); Short(b,0); Point(b,0,1,0); Short(b,i); Short(b,0); }
    for(int i=0;i<3;++i) { Float(b,static_cast<float>(i==1)); Float(b,static_cast<float>(i==2)); }
    for(int i=0;i<faces;++i)
    {
        Bytes f{3,0}; for(int group=0;group<3;++group) { Short(f,0);Short(f,1);Short(f,2);Short(f,0); }
        f.resize(64);b.insert(b.end(),f.begin(),f.end());
    }
    Name(b,texture); Short(b,1);b.push_back(0);
    b.push_back(0);Name(b,"root");Short(b,-1);Point(b,0,0,0);Point(b,0,0,0);
    return b;
}
Bytes Encrypted(const Bytes& plain)
{
    const auto encrypted = WorldObjectCodec::Encrypt(std::span<const std::uint8_t>(plain).subspan(4));
    Bytes result{'B','M','D',12};Number(result,static_cast<std::uint32_t>(encrypted.size()),4);
    result.insert(result.end(),encrypted.begin(),encrypted.end());return result;
}
void Put(const std::filesystem::path& path,const Bytes& bytes)
{
    std::filesystem::create_directories(StreamPath(path.parent_path()));
    std::ofstream f(StreamPath(path),std::ios::binary);f.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
    if(!f)throw std::runtime_error("fixture write failed");
}
void PoseTests()
{
    auto b=Fixture();b[38]=2;
    for(const auto offset:{52,68,84})b[static_cast<std::size_t>(offset)]=1;
    b.resize(b.size()-59);
    b.push_back(0);Name(b,"root");Short(b,-1);Point(b,10,20,30);Point(b,0,0,1.57079632679f);
    b.push_back(0);Name(b,"child");Short(b,0);Point(b,2,0,0);Point(b,0,0,0);
    const auto model=WorldModelPreview::Decode(b);
    const auto& t=model.Geometry.at(0);
    Check(std::abs(t[0][0]-10)<0.0001f&&std::abs(t[0][1]-22)<0.0001f&&std::abs(t[0][2]-30)<0.0001f,
        "parent rotation transforms child translation before vertex skinning");
    Check(std::abs(t[1][0]-10)<0.0001f&&std::abs(t[1][1]-23)<0.0001f&&std::abs(t[2][2]-31)<0.0001f,
        "triangle vertices follow initial bone pose without spikes");
    Check(model.Geometry==WorldModelPreview::Decode(Encrypted(b)).Geometry,"encrypted BMD preserves reference pose");
    Check(WorldNpcVisualPath(3)=="Monster/Monster10.bmd"&&WorldNpcVisualPath(251)=="Npc/Smith01.bmd",
        "NPC server IDs map to actual model filenames, never filename number guesses");
    Check(WorldNpcVisualPath(32767).empty()&&WorldNpcVisualPath(255).empty(),"unknown and multipart NPCs stay explicitly unresolved");
}
void DecodingTests()
{
    const auto plain=Fixture(); const auto a=WorldModelPreview::Decode(plain);const auto b=WorldModelPreview::Decode(Encrypted(plain));
    Check(a.Version==10 && b.Version==12,"both actual BMD encodings supported");
    Check(a.MeshCount==1 && a.BoneCount==1 && a.ActionCount==1,"model counts");
    Check(a.VertexCount==3 && a.TriangleCount==1,"mesh counts");
    Check(a.Textures==b.Textures && a.Textures[0]=="bark.jpg","texture names preserved");
    Check(a.Geometry==b.Geometry && a.Geometry.size()==1,"geometry identical across encryption");
    Check(a.Geometry[0][1][0]==1 && a.Geometry[0][2][2]==1,"vertex offsets match disk layout");
    for(std::size_t size=0;size<plain.size();++size)
        Reject([&]{WorldModelPreview::Decode(std::span<const std::uint8_t>(plain).first(size));},"every truncated prefix rejected");
    const auto encrypted=Encrypted(plain);
    for(std::size_t size=0;size<encrypted.size();++size)
        Reject([&]{WorldModelPreview::Decode(std::span<const std::uint8_t>(encrypted).first(size));},"every encrypted truncated prefix rejected");
    auto bad=plain;bad.push_back(0);Reject([&]{WorldModelPreview::Decode(bad);},"unparsed trailing bytes rejected");
    bad=plain;bad[3]=14;Reject([&]{WorldModelPreview::Decode(bad);},"unsupported v14 never passed to engine");
    bad=Encrypted(plain);bad[4]^=1;Reject([&]{WorldModelPreview::Decode(bad);},"encrypted length mismatch rejected");
    bad=plain;bad[36]=51;Reject([&]{WorldModelPreview::Decode(bad);},"mesh budget enforced");
    bad=plain;bad[38]=201;Reject([&]{WorldModelPreview::Decode(bad);},"bone budget enforced");
    bad=plain;bad[42]=0xff;bad[43]=0xff;Reject([&]{WorldModelPreview::Decode(bad);},"negative vertex count rejected");
    bad=plain;bad[52]=1;Reject([&]{WorldModelPreview::Decode(bad);},"vertex bone outside model rejected");
    bad=plain;const auto nan=std::bit_cast<std::uint32_t>(std::numeric_limits<float>::quiet_NaN());
    for(std::size_t i=0;i<4;++i)bad[56+i]=static_cast<std::uint8_t>(nan>>(8*i));
    Reject([&]{WorldModelPreview::Decode(bad);},"non-finite vertex rejected");
    constexpr std::size_t FaceOffset=52+3*16+3*20+3*8;
    bad=plain;bad[FaceOffset]=4;Reject([&]{WorldModelPreview::Decode(bad);},"quad is explicitly unsupported");
    bad=plain;bad[FaceOffset+2]=9;Reject([&]{WorldModelPreview::Decode(bad);},"vertex face index rejected");
    bad=plain;bad[FaceOffset+10]=9;Reject([&]{WorldModelPreview::Decode(bad);},"normal face index rejected");
    bad=plain;bad[FaceOffset+18]=9;Reject([&]{WorldModelPreview::Decode(bad);},"UV face index rejected");
    constexpr std::size_t ActionOffset=FaceOffset+64+32;
    bad=plain;bad[ActionOffset+2]=2;Reject([&]{WorldModelPreview::Decode(bad);},"invalid action bool rejected");
    bad=plain;bad[ActionOffset+3]=1;Reject([&]{WorldModelPreview::Decode(bad);},"used dummy bone rejected");
    bad=plain;bad[ActionOffset+3+1+32]=0;bad[ActionOffset+3+1+32+1]=0;
    Reject([&]{WorldModelPreview::Decode(bad);},"cyclic bone parent rejected");
    const auto many=WorldModelPreview::Decode(Fixture("bark.jpg",500));
    Check(many.TriangleCount==500 && many.Geometry.size()<=WorldModelPreview::MaximumPreviewTriangles,"bounded preview without bypassing full validation");
}
void FileTests(const std::filesystem::path& root)
{
    using State=WorldModelInspection::TextureState;
    const auto model=root/"Object3/Object01.bmd";Put(model,Encrypted(Fixture()));
    auto result=WorldModelInspection::Read(root,"Object3/Object01.bmd");
    Check(result.Textures[0].State==State::Missing && result.HasMissingDependencies(),"missing texture diagnosed without creating it");
    Put(root/"Object3/bark.jpg",Bytes{1,2,3});result=WorldModelInspection::Read(root,"Object3/Object01.bmd");
    Check(result.Textures[0].State==State::RawOnly,"raw JPG does not pretend to be the OZJ loaded by MU");
    Put(root/"Object3/BARK.OZJ",Bytes{1,2,3});result=WorldModelInspection::Read(root,"Object3/Object01.bmd");
    Check(result.Textures[0].State==State::PackedFilePresent && !result.HasMissingDependencies(),"packed filename lookup is case insensitive");
    Check(result.Textures[0].RelativePath=="Object3/BARK.OZJ","exact existing filename reported");
    Check(result.Model.Geometry.size()==1 && result.Bytes==Encrypted(Fixture()).size(),"file inspection preserves geometry and size");
    for(const auto& name:{"../Object3/Object01.bmd","Object3/../Object3/Object01.bmd","C:/Object3/Object01.bmd","Object3\\Object01.bmd","Object3/Object01.exe","Object3/sub/Object01.bmd"})
        Reject([&]{WorldModelInspection::Read(root,name);},"escaped or executable model path rejected");
    Put(root/"Object3/Unsafe.bmd",Fixture("../escape.tga"));result=WorldModelInspection::Read(root,"Object3/Unsafe.bmd");
    Check(result.Textures[0].State==State::UnsupportedName,"texture traversal not followed");
    Check(!std::filesystem::exists(root/"escape.tga"),"no files written by inspection");
    Put(root/"Object3/Special.bmd",Fixture("skin"));result=WorldModelInspection::Read(root,"Object3/Special.bmd");
    Check(result.Textures[0].State==State::UnsupportedName,"special texture names reported honestly");
    std::error_code error;
    std::filesystem::create_directory_symlink(root/"Object3",root/"Object4",error);
    if (!error) Reject([&]{WorldModelInspection::Read(root,"Object4/Object01.bmd");},"model directory symlink not followed");
    else std::cout << "SYMLINK_TEST_SKIPPED " << error.message() << '\n';
#ifdef _WIN32
    Reject([&]{WorldModelInspection::Read(LR"(\\?\GLOBALROOT\Device)","Object3/Object01.bmd");},"device paths rejected before traversal");
    Reject([&]{WorldModelInspection::Read(LR"(\\?\UNC\server\share)","Object3/Object01.bmd");},"UNC paths rejected before traversal");
#endif
    const auto longRoot=root/"long"/std::string(120,'a')/std::string(120,'b');Put(longRoot/"Object2/tree.bmd",Fixture());
    Check(WorldModelInspection::Read(longRoot,"Object2/tree.bmd").Model.VertexCount==3,"long LAB paths supported");
}
void RealTests(const std::filesystem::path& data)
{
    std::set<std::string> inspected;
    std::size_t ready=0,missing=0,unsupported=0;
    for(const auto& binding:WorldNpcVisualBindings)
    {
        const std::string path(binding.Path);if(!inspected.insert(path).second)continue;
        if(!std::filesystem::is_regular_file(data/path)){++missing;continue;}
        try{const auto preview=WorldModelInspection::ReadCandidate(data/path);Check(!preview.Model.Geometry.empty(),"NPC mesh has posed preview triangles");++ready;}
        catch(const std::exception&){++unsupported;}
    }
    Check(ready>20,"actual NPC model coverage is nonempty");
    std::cout<<"NPC_PREVIEW_AUDIT ready="<<ready<<" missing="<<missing<<" unsupported="<<unsupported<<'\n';
    struct Expected {const char* Path;int Version,Meshes,Bones;std::size_t Vertices,Triangles;};
    const Expected tests[]{
        {"Object1/Beer01.bmd",10,5,5,139,216}, {"Object2/Bat01.bmd",10,2,8,32,38},
        {"Object2/DungeonStone01.bmd",10,1,1,6,8}, {"Object3/Object01.bmd",12,2,12,138,140},
        {"Object3/Object02.bmd",12,2,1,28,44}, {"Object4/Object01.bmd",10,2,5,132,134},
        {"Object4/Object02.bmd",10,2,8,48,69}, {"Object8/Fish02.bmd",10,1,5,17,30}};
    for(const auto& item:tests)
    {
        const auto result=WorldModelInspection::Read(data,item.Path);const auto& model=result.Model;
        Check(model.Version==item.Version && model.MeshCount==item.Meshes && model.BoneCount==item.Bones,item.Path);
        Check(model.VertexCount==item.Vertices && model.TriangleCount==item.Triangles,"matches independent Python inspector counts");
        Check(!model.Geometry.empty() && model.Geometry.size()<=WorldModelPreview::MaximumPreviewTriangles,"real model preview bounded");
        std::cout<<"REAL_PREVIEW_OK "<<item.Path<<" textures="<<result.Textures.size()<<" needs_review="<<result.HasMissingDependencies()<<'\n';
    }
}
}
int main(int argc,char** argv)
{
    try
    {
        const auto stamp=std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        const auto root=std::filesystem::current_path()/("model-preview-tests-"+stamp);
        PoseTests();DecodingTests();FileTests(root);
        if(argc>1 && std::string(argv[1]).size())RealTests(argv[1]);
        std::cout<<"ALL MODEL PREVIEW CHECKS PASSED: "<<checks<<'\n';return 0;
    }
    catch(const std::exception& error){std::cerr<<"MODEL_PREVIEW_TEST_FAILURE "<<error.what()<<'\n';return 1;}
}
