#pragma once
#include"Movement/Movements.h"
#include"GraphicsContext/GraphicsContext.h"
#define RENDEREVENT_CLASS_TYPE(type) static RenderType GetStaticType() { return RenderType::type; }\
								virtual RenderType GetType() const override { return GetStaticType(); }\
								virtual const char* GetName() const override { return #type; }
namespace X_Y {

		enum class RenderType {
			RenderON,RenderStart
		};
	struct RenderEvent :public Event {
		RenderEvent(EventSender s) :Event(s) {};
		~RenderEvent() = default;
		virtual RenderType GetType() const = 0;
		MOVEMENT_CLASS_CATEGORY(MTApplication)
		EVENT_DISPATHC_INIT
	};
	struct RenderON :public RenderEvent {
		RenderON(EventSender s) :RenderEvent(s) {};
		RENDEREVENT_CLASS_TYPE(RenderON)
	};
	struct RenderStart :public RenderEvent {
		RenderStart(EventSender s, GraphicsContext* grContext, int width, int height) :RenderEvent(s), m_GraphicsContext(grContext), m_Width(width), m_Height(height) {};
		~RenderStart() {
			m_GraphicsContext = nullptr;
		}
		GraphicsContext* m_GraphicsContext;
		int m_Width;
		int m_Height;
		RENDEREVENT_CLASS_TYPE(RenderStart)
	
	};

}
