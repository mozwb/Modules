#pragma once
#include<Core/Core.h>
#include<Render/renderMath/RenderMath.h>
#include<unordered_map>
#include<string>
namespace X_Y {
	class Shader
	{
	public:
		virtual ~Shader() = default;

		virtual void Bind() const = 0;
		virtual void Unbind() const = 0;

		virtual void SetInt(const std::string& name, int value) = 0;
		virtual void SetIntArray(const std::string& name, int* values, uint32_t count) = 0;
		virtual void SetFloat(const std::string& name, float value) = 0;
		virtual void SetFloat2(const std::string& name, const RenderMath::Vec2& value) = 0;
		virtual void SetFloat3(const std::string& name, const RenderMath::Vec3& value) = 0;
		virtual void SetFloat4(const std::string& name, const RenderMath::Vec4& value) = 0;
		virtual void SetMat4(const std::string& name, const RenderMath::Mat4& value) = 0;

		virtual const std::string& GetName() const = 0;

		static Ref<Shader> Create(const std::string& filepath);
		static Ref<Shader> Create(const std::string& name, const std::string& vertexSrc, const std::string& fragmentSrc);
	};

	class ShaderLibrary
	{
	public:
		void Add(const std::string& name, const Ref<Shader>& shader);
		void Add(const Ref<Shader>& shader);
		Ref<Shader> Load(const std::string& filepath);
		Ref<Shader> Load(const std::string& name, const std::string& filepath);
		/// 加载目录下所有 .glsl 文件，按文件名（不含扩展名）注册
		void LoadDirectory(const std::string& directory);

		Ref<Shader> Get(const std::string& name);

		bool Exists(const std::string& name) const;

		/// 遍历所有已注册的 shader 名称
		const auto& GetAll() const { return m_Shaders; }
	private:
		std::unordered_map<std::string, Ref<Shader>> m_Shaders;
	};
}
