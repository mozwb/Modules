#include "APP/Library.h"

#include <utility>

namespace X_Y::App
{
    Library::~Library()
    {
        Close();
    }

    Library::Library(Library &&other) noexcept
        : m_Handle(other.m_Handle), m_Path(std::move(other.m_Path))
    {
        other.m_Handle = nullptr;
    }

    Library &Library::operator=(Library &&other) noexcept
    {
        if (this != &other)
        {
            Close();
            m_Handle = other.m_Handle;
            m_Path = std::move(other.m_Path);
            other.m_Handle = nullptr;
        }
        return *this;
    }
}
