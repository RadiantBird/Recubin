#pragma once
#include <include/Instances/Instance.hpp>
#include <include/Instances/Workspace.hpp>

class Script : public Instance {
    public:
        string Source = R"(print("Hello world!"))";
        Workspace* lastWorkspace = nullptr;

        bool Enabled = true;
        
        virtual string GetClassName() override;
        bool IsA(std::string className) override;
        void onAncestorChanged() override;
};
