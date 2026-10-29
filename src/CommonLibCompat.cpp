#include "PCH.h"

// CommonLibSSE-NG 3.5.2 declares ExtraDataList's ctor/dtor in the public
// headers, but the vcpkg package used by TradeTogether does not export the
// corresponding definitions. Provide the same relocation-backed definitions
// used by newer CommonLib versions.
//
// Skyrim SE/AE 1.6.1170 uses the AE layout/path here.
namespace RE
{
    ExtraDataList::ExtraDataList()
    {
        using func_t = void(ExtraDataList*);
        static REL::Relocation<func_t> func{
            RELOCATION_ID(11437, 11583)
        };
        func(this);
    }

    ExtraDataList::~ExtraDataList()
    {
        using func_t = void(ExtraDataList*);
        static REL::Relocation<func_t> func{
            RELOCATION_ID(11438, 11584)
        };
        func(this);
    }
}
