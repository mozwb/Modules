#include "Json/json.h"
#include "IO/Files.h"
#include <iostream>

int main()
{
    X_Y::XPath jfile("D:/workbench/Modules/.vscode/c_cpp_properties.json");
    X_Y::Buffer buf = jfile.ReadBinary();
    // buf.As<char>() 拿到字符串视图
    std::string str(buf.As<char>(), static_cast<size_t>(buf.Size));

    X_Y::json parser;
    X_Y::JsonValue js = parser.Parse(str);

    const std::string sample = R"({"items":[1,true,null,"value"]})";
    if (parser.Parse(sample).ToString() != sample)
        return 1;

    std::cout << js.ToString() << std::endl;

    return 0;
}