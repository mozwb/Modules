#include "APP/Library.h"

#include <dlfcn.h>

namespace X_Y::App
{
    bool Library::Open(const std::string &path, std::string *error)
    {
        Close();

        void *handle = ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!handle)
        {
            const char *reason = ::dlerror();
            if (error)
            {
                *error = reason ? reason : "dlopen 失败";
            }
            return false;
        }

        m_Handle = handle;
        m_Path = path;
        return true;
    }

    void Library::Close()
    {
        if (m_Handle)
        {
            ::dlclose(m_Handle);
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
        return ::dlsym(m_Handle, name);
    }
}
