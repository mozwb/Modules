#pragma once
#include "Widget/Movement/Movements.h"
#include"UI/imgui/imguibuild.h"
namespace X_Y {
	class ImGuiLayer : public Layer {
	public:
		ImGuiLayer();
		ImGuiLayer(void* hwnd);
		~ImGuiLayer() = default;

		void SetHwnd(void* hwnd) { m_Hwnd = hwnd; }

		virtual void OnAttach() override;
		virtual void OnDetach() override;
		virtual void OnEvent(XMovement* e) override;

		void Begin();
		void End();

		void BlockEvents(bool block) { m_BlockEvents = block; }

		void SetDarkThemeColors();

		uint32_t GetActiveWidgetID() const;
	private:
		void* m_Hwnd = nullptr;
		bool m_BlockEvents = true;

	};
}
