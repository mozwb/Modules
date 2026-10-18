#include "Network/NetworkError.h"

#include <string>
#include <utility>

namespace X_Y
{
    NetworkError::NetworkError(
        std::string operation,
        int errorCode,
        std::string detail)
        : std::runtime_error(
              operation + " failed (error " + std::to_string(errorCode) + "): " + detail),
          m_Operation(std::move(operation)),
          m_ErrorCode(errorCode)
    {
    }

}
