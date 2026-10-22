#include<imgui/ImGuiLayer.h>
#include<imgui_internal.h>
#include<Widget/src/Win32/Win32Globals.h>

// ImGui_ImplWin32_WndProcHandler 声明在头文件中被 #if 0 禁用，需手动 extern
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace X_Y {

	// 注册 WndProc hook：让 Window 模块把消息先给 ImGui 处理
	static void RegisterWndProcHook()
	{
		Win32::g_WndProcHook = [](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) -> LRESULT {
			// ImGui_ImplWin32_WndProcHandler 内部所有分支都 return 0，
			// 所以我们不在 WndProc 层拦截任何消息。所有消息都会
			// 正常入队成 Movement，在 ImGuiLayer::OnEvent 中
			// 用 WantCaptureMouse/Keyboard 做二次过滤。
			ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam);
			return false;
		};
	}

	ImGuiLayer::ImGuiLayer()
		: Layer()
	{
		RegisterWndProcHook();
	}

	ImGuiLayer::ImGuiLayer(void* hwnd)
		: Layer(), m_Hwnd(hwnd)
	{
		RegisterWndProcHook();
	}

	void ImGuiLayer::OnAttach()
	{
		// 在此完成 ImGui 初始化，不用外部调 ImGuiBuild::Init
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO(); (void)io;
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

		SetDarkThemeColors();

		ImGui_ImplWin32_InitForOpenGL(m_Hwnd);
		XINFO("ImGui 已初始化");
		ImGui_ImplOpenGL3_Init("#version 330");
	}

	void ImGuiLayer::OnDetach()
	{
		// 在此清理，不用外部调 ImGuiBuild::Shutdown
		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
		XINFO("ImGui 已清理");
	}

	void ImGuiLayer::OnEvent(XMovement* e)
	{
		if (!m_BlockEvents)
			return;

		ImGuiIO& io = ImGui::GetIO();
		if (auto* movement = dynamic_cast<Movement*>(e))
		{
			if (movement->IsInCategory(MTMouse))
				e->Handled |= io.WantCaptureMouse;
			if (movement->IsInCategory(MTKeyboard))
				e->Handled |= io.WantCaptureKeyboard;
		}
	}

	void ImGuiLayer::Begin()
	{
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();
	}

	void ImGuiLayer::End()
	{
		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
	}

	void ImGuiLayer::SetDarkThemeColors()
	{
		ImGui::StyleColorsDark();
	}

	uint32_t ImGuiLayer::GetActiveWidgetID() const
	{
		return ImGui::GetActiveID();
	}

}
