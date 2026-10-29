#pragma once
#include "Widget/Movement/Movements.h"
#include "Input/Input.h"
namespace X_Y
{
	using KeyCode = Input::KeyCode;
	class KeyMovement : public Movement
	{
	public:
		KeyCode GetKeyCode() const { return m_KeyCode; }

		MOVEMENT_CLASS_CATEGORY(MTKeyboard | MTInput)
	protected:
		KeyMovement(MovementSender s, const KeyCode keycode)
			: m_KeyCode(keycode), Movement(s)
		{
		}
		KeyCode m_KeyCode;
	};
	class KeyPressed : public KeyMovement
	{
	public:
		KeyPressed(MovementSender s, const KeyCode keycode, bool isRepeat = false) : KeyMovement(s, keycode), m_IsRepeat(isRepeat) {}
		bool IsRepeat() const { return m_IsRepeat; }
		std::string toString() const override
		{
			std::stringstream ss;
			ss << "KeyPressed: " << m_KeyCode << " (repeat = " << m_IsRepeat << ")";
			return ss.str();
		}
		MOVEMENT_CLASS_TYPE(KeyPressed)
	private:
		bool m_IsRepeat;
	};
	class KeyReleased : public KeyMovement
	{
	public:
		KeyReleased(MovementSender s, const KeyCode keycode) : KeyMovement(s, keycode) {}
		std::string toString() const override
		{
			std::stringstream ss;
			ss << "KeyReleased: " << m_KeyCode;
			return ss.str();
		}
		MOVEMENT_CLASS_TYPE(KeyReleased)
	};
	class KeyTyped : public KeyMovement
	{
	public:
		KeyTyped(MovementSender s, const KeyCode keycode) : KeyMovement(s, keycode)
		{
		}
		std::string toString() const override
		{
			std::stringstream ss;
			ss << "KeyTyped: " << m_KeyCode;
			return ss.str();
		}
		MOVEMENT_CLASS_TYPE(KeyTyped)
	};
}
