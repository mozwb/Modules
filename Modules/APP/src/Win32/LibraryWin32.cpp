#include "APP/Library.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace X_Y::App
{
    bool Library::Open(const std::string &path, std::string *error)
    {
        Close();

        HMODULE handle = ::LoadLibraryA(path.c_str());
        if (!handle)
        {
            if (error)
            {
                *error = "LoadLibrary 失败，错误码 " +
                         std::to_string(::GetLastError());
            }
            return false;
        }

        m_Handle = reinterpret_cast<void *>(handle);
        m_Path = path;
        return true;
    }

    void Library::Close()
    {
        if (m_Handle)
        {
            ::FreeLibrary(static_cast<HMODULE>(m_Handle));
            m_Handle = nullptr;
        }
        m_Path.clear();
    }

    void *Library::Symbol(const char *name) const
    {
        if (!m_Handle || !name)
        {
            return nullptr;
        }
        return reinterpret_cast<void *>(
            ::GetProcAddress(static_cast<HMODULE>(m_Handle), name));
    }
}
