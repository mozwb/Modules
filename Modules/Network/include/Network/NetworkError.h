#pragma once

#include <stdexcept>
#include <string>

namespace X_Y
{
    class NetworkError : public std::runtime_error
    {
    public:
        NetworkError(std::string operation, int errorCode, std::string detail);

        int ErrorCode() const noexcept { return m_ErrorCode; }
        const std::string &Operation() const noexcept { return m_Operation; }

    private:
        std::string m_Operation;
        int m_ErrorCode;
    };
}
