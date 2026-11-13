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

    // 方案2：手动遍历根Object，就是你想要的循环写法
    std::cout << "\n===== Manual Iterate Root Object =====\n";
    if (js.type == X_Y::JsonValue::Type::Object)
    {
        const X_Y::JsonObject &rootObj = js.Get<X_Y::JsonObject>();
        std::cout << rootObj.at("version").Get<std::string>() << std::endl;
    }

    return 0;
}