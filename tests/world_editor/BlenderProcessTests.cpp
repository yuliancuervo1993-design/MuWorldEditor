#include "WorldBlenderProcess.h"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace Silver::WorldEditor;

int main(int argc,char** argv)
{
    if(argc==2){std::this_thread::sleep_for(std::chrono::milliseconds(250));return std::stoi(argv[1]);}
    try
    {
        const auto executable=std::filesystem::absolute(argv[0]);
        const auto state=std::filesystem::current_path()/"blender-process-test-state.json";
        WorldBlenderProcess child;
        for(const auto code:{0,7})
        {
            child.Start(executable,std::to_wstring(code),state);
            if(!child.Running()||child.ProcessId()==0)throw std::runtime_error("Missing child identity");
            bool duplicateBlocked=false;
            try{child.Start(executable,L"0",state);}catch(const std::exception&){duplicateBlocked=true;}
            if(!duplicateBlocked)throw std::runtime_error("Duplicate child accepted");
            const auto limit=std::chrono::steady_clock::now()+std::chrono::seconds(10);
            while(child.Running()&&std::chrono::steady_clock::now()<limit)
            {std::this_thread::sleep_for(std::chrono::milliseconds(10));child.Poll();}
            if(child.Running()||child.ExitCode()!=static_cast<unsigned long>(code))throw std::runtime_error("Wrong child exit status");
        }
        std::cout<<"BlenderProcessTests PASS: real finite child, duplicate rejection, exit 0/7, no child left running\n";
        return 0;
    }
    catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}
}
