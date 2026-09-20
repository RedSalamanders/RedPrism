#pragma once

#include <zip.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/resource.h>
#pragma warning(pop)

inline void ZipDiscard(zip_t* zip) noexcept
{
    if (zip)
    {
        zip_discard(zip);
    }
}

inline void ZipFileClose(zip_file_t* file) noexcept
{
    if (file)
    {
        zip_fclose(file);
    }
}

using unique_zip = wil::unique_any<zip_t*, decltype(&ZipDiscard), ZipDiscard>;
using unique_zip_file = wil::unique_any<zip_file_t*, decltype(&ZipFileClose), ZipFileClose>;

[[nodiscard]] inline bool ZipCommit(unique_zip& owner) noexcept
{
    zip_t* raw = owner.release();
    if (raw == nullptr)
    {
        return true;
    }
    if (zip_close(raw) == 0)
    {
        return true;
    }
    zip_discard(raw);
    return false;
}
