#include "APP/MicroProgram.h"

namespace X_Y::App
{
    bool MicroProgram::Load(const std::string &path, const char *entry,
                            std::string *error)
    {
        Unload();

        if (!entry || !*entry)
        {
            if (error)
            {
                *error = "入口符号名为空";
            }
            return false;
        }

        if (!m_Library.Open(path, error))
        {
            return false;
        }

        void *symbol = m_Library.Symbol(entry);
        if (!symbol)
        {
            if (error)
            {
                *error = "没有导出入口 '" + std::string(entry) + "'";
            }
            m_Library.Close();
            return false;
        }

        m_Entry = reinterpret_cast<Entry>(symbol);
        return true;
    }

    void MicroProgram::Unload()
    {
        m_Entry = nullptr;
        m_Library.Close();
    }

    int MicroProgram::Run(const void *host, int argc,
                          const char *const *argv) const
    {
        if (!m_Entry)
        {
            return -1;
        }
        return m_Entry(host, argc, argv);
    }

    int MicroProgram::Run(const void *host,
                          const std::vector<std::string> &args) const
    {
        std::vector<const char *> argv;
        argv.reserve(args.size() + 1);
        for (const auto &arg : args)
        {
            argv.push_back(arg.c_str());
        }
        argv.push_back(nullptr);

        return Run(host, static_cast<int>(args.size()), argv.data());
    }
}
