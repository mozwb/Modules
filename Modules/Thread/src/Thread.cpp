#include "Thread/Thread.h"

namespace X_Y
{
    void Thread::Join()
    {
        if (m_Thread.joinable())
            m_Thread.join();
    }
}
