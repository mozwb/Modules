#pragma once
#include "Widget/Movement/Movements.h"
namespace X_Y
{
	class AppTick : public Movement
	{
	public:
		AppTick(MovementSender s) : Movement(s) {};

		MOVEMENT_CLASS_TYPE(AppTick)
		MOVEMENT_CLASS_CATEGORY(MTApplication)
	};

	class AppUpdate : public Movement
	{
	public:
		AppUpdate(MovementSender s) : Movement(s) {};

		MOVEMENT_CLASS_TYPE(AppUpdate)
		MOVEMENT_CLASS_CATEGORY(MTApplication)
	};

	class AppRender : public Movement
	{
	public:
		AppRender(MovementSender s) : Movement(s) {};

		MOVEMENT_CLASS_TYPE(AppRender)
		MOVEMENT_CLASS_CATEGORY(MTApplication)
	};

	class WindowResize : public Movement
	{
	public:
		WindowResize(MovementSender s, unsigned int width, unsigned int height)
			: m_Width(width), m_Height(height), Movement(s)
		{
		}

		unsigned int GetWidth() const { return m_Width; }
		unsigned int GetHeight() const { return m_Height; }

		std::string toString() const override
		{
			std::stringstream ss;
			ss << "WindowResizeEvent: " << m_Width << ", " << m_Height;
			return ss.str();
		}
		MOVEMENT_CLASS_TYPE(WindowResize)
		MOVEMENT_CLASS_CATEGORY(MTWindow)

	private:
		unsigned int m_Width, m_Height;
	};

	class WindowClose : public Movement
	{
	public:
		WindowClose(MovementSender s) : Movement(s) {}

		MOVEMENT_CLASS_TYPE(WindowClose)
		MOVEMENT_CLASS_CATEGORY(MTWindow)
	};

	class WindowDestroy : public Movement
	{
	public:
		WindowDestroy(MovementSender s) : Movement(s) {}

		MOVEMENT_CLASS_TYPE(WindowDestroy)
		MOVEMENT_CLASS_CATEGORY(MTWindow)
	};

	// 暂且不知道这俩有啥用
	class WindowFouces : public Movement
	{
	public:
		WindowFouces(MovementSender s) : Movement(s) {}

		MOVEMENT_CLASS_TYPE(WindowFocus)
		MOVEMENT_CLASS_CATEGORY(MTWindow)
	};

	class WindowMoved : public Movement
	{
	public:
		WindowMoved(MovementSender s) : Movement(s) {}

		MOVEMENT_CLASS_TYPE(WindowMoved)
		MOVEMENT_CLASS_CATEGORY(MTWindow)
	};

	class WindowPaint : public Movement
	{
	public:
		WindowPaint(MovementSender s) : Movement(s) {}
		MOVEMENT_CLASS_TYPE(WindowPaint)
		MOVEMENT_CLASS_CATEGORY(MTWindow)
	};

	class WindowDragBegin : public Movement
	{
	public:
		WindowDragBegin(MovementSender s) : Movement(s) {}
		MOVEMENT_CLASS_TYPE(WindowDragBegin)
		MOVEMENT_CLASS_CATEGORY(MTWindow)
	};

	class WindowDragEnd : public Movement
	{
	public:
		WindowDragEnd(MovementSender s) : Movement(s) {}
		MOVEMENT_CLASS_TYPE(WindowDragEnd)
		MOVEMENT_CLASS_CATEGORY(MTWindow)
	};
}
